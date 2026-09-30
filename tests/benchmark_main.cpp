#include "core/store.hpp"
#include "core/evaluator.hpp"
#include "core/client_context.hpp"
#include "server/server.hpp"
#include "server/shutdown.hpp"
#include "logger.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <functional>

namespace {

// Terminal styling
namespace color {
    inline constexpr const char* RESET   = "\033[0m";
    inline constexpr const char* BOLD    = "\033[1m";
    inline constexpr const char* RED     = "\033[31m";
    inline constexpr const char* GREEN   = "\033[32m";
    inline constexpr const char* YELLOW  = "\033[33m";
    inline constexpr const char* CYAN    = "\033[36m";
    inline constexpr const char* MAGENTA = "\033[35m";
}

double get_process_rss_mb() {
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) return 0.0;
    size_t size_pages = 0;
    size_t resident_pages = 0;
    statm >> size_pages >> resident_pages;
    long page_size = sysconf(_SC_PAGESIZE);
    return static_cast<double>(resident_pages * page_size) / (1024.0 * 1024.0);
}

struct LatencyStats {
    double min_ms{0.0};
    double avg_ms{0.0};
    double p50_ms{0.0};
    double p90_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
    double max_ms{0.0};
    double rps{0.0};
    size_t total_ops{0};
    double duration_s{0.0};
};

LatencyStats compute_stats(std::vector<double>& latencies_ms, double total_seconds) {
    LatencyStats st;
    st.total_ops = latencies_ms.size();
    st.duration_s = total_seconds;
    if (latencies_ms.empty()) return st;

    std::sort(latencies_ms.begin(), latencies_ms.end());

    double sum = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0);
    st.avg_ms = sum / latencies_ms.size();
    st.min_ms = latencies_ms.front();
    st.max_ms = latencies_ms.back();
    st.rps = (total_seconds > 0) ? (static_cast<double>(st.total_ops) / total_seconds) : 0.0;

    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * (latencies_ms.size() - 1));
        return latencies_ms[idx];
    };

    st.p50_ms = percentile(0.50);
    st.p90_ms = percentile(0.90);
    st.p95_ms = percentile(0.95);
    st.p99_ms = percentile(0.99);

    return st;
}

void print_benchmark_table_header() {
    std::cout << color::BOLD
              << "+------------+------------+----------+----------+----------+----------+----------+----------+\n"
              << "| Command    | RPS        | Avg (ms) | Min (ms) | p50 (ms) | p90 (ms) | p99 (ms) | Max (ms) |\n"
              << "+------------+------------+----------+----------+----------+----------+----------+----------+\n"
              << color::RESET;
}

void print_benchmark_row(const std::string& cmd, const LatencyStats& s) {
    std::cout << "| " << std::left << std::setw(10) << cmd
              << " | " << std::right << std::setw(10) << std::fixed << std::setprecision(1) << s.rps
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.avg_ms
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.min_ms
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.p50_ms
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.p90_ms
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.p99_ms
              << " | " << std::setw(8) << std::fixed << std::setprecision(3) << s.max_ms
              << " |\n";
}

void print_benchmark_table_footer() {
    std::cout << color::BOLD
              << "+------------+------------+----------+----------+----------+----------+----------+----------+\n"
              << color::RESET;
}

// -------------------------------------------------------------
// Direct Engine Microbenchmarks (In-Memory without TCP stack)
// -------------------------------------------------------------
void run_engine_microbenchmarks(size_t num_ops) {
    std::cout << color::BOLD << color::CYAN
              << "\n========================================================================================\n"
              << "      RunDB Native Engine Microbenchmarks (Direct In-Memory / Zero Network Overhead)    \n"
              << "========================================================================================\n"
              << color::RESET
              << "Operations per test: " << num_ops << "\n\n";

    rundb::core::Store store;
    rundb::core::ClientContext ctx;

    struct TestDef {
        std::string name;
        std::function<std::vector<std::string>(size_t)> gen_cmd;
    };

    std::vector<TestDef> tests = {
        {"PING",      [](size_t) { return std::vector<std::string>{"PING"}; }},
        {"SET",       [](size_t i) { return std::vector<std::string>{"SET", "key_" + std::to_string(i), "val_" + std::to_string(i)}; }},
        {"GET",       [](size_t i) { return std::vector<std::string>{"GET", "key_" + std::to_string(i)}; }},
        {"INCR",      [](size_t) { return std::vector<std::string>{"INCR", "bench_counter"}; }},
        {"LPUSH",     [](size_t i) { return std::vector<std::string>{"LPUSH", "bench_list", "item_" + std::to_string(i)}; }},
        {"LPOP",      [](size_t) { return std::vector<std::string>{"LPOP", "bench_list"}; }},
        {"SADD",      [](size_t i) { return std::vector<std::string>{"SADD", "bench_set", std::to_string(i)}; }},
        {"SISMEMBER", [](size_t i) { return std::vector<std::string>{"SISMEMBER", "bench_set", std::to_string(i)}; }},
    };

    print_benchmark_table_header();

    double rss_start = get_process_rss_mb();
    double peak_rss = rss_start;

    for (const auto& t : tests) {
        std::vector<double> latencies_ms;
        latencies_ms.reserve(num_ops);

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_ops; ++i) {
            auto cmd = t.gen_cmd(i);
            auto t0 = std::chrono::high_resolution_clock::now();
            rundb::core::Evaluator::evaluate(store, ctx, cmd);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            latencies_ms.push_back(ms);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double total_sec = std::chrono::duration<double>(end - start).count();

        LatencyStats stats = compute_stats(latencies_ms, total_sec);
        print_benchmark_row(t.name, stats);

        double cur_rss = get_process_rss_mb();
        if (cur_rss > peak_rss) peak_rss = cur_rss;
    }

    print_benchmark_table_footer();
    std::cout << "Memory: Baseline RSS = " << std::fixed << std::setprecision(2) << rss_start
              << " MB | Peak RSS = " << peak_rss
              << " MB | Delta = " << (peak_rss - rss_start) << " MB\n";
}

// -------------------------------------------------------------
// Native TCP Network Benchmark (High Performance Socket Client)
// -------------------------------------------------------------
int connect_socket(const std::string& host, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flag = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

LatencyStats run_tcp_command_benchmark(
    const std::string& host,
    uint16_t port,
    const std::string& /*cmd_name*/,
    const std::string& raw_resp_cmd,
    size_t total_requests,
    size_t num_clients
) {
    size_t requests_per_client = total_requests / num_clients;
    std::vector<std::thread> workers;
    std::vector<std::vector<double>> client_latencies(num_clients);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t c = 0; c < num_clients; ++c) {
        workers.emplace_back([&, c]() {
            client_latencies[c].reserve(requests_per_client);
            int fd = connect_socket(host, port);
            if (fd < 0) return;

            char buf[4096];
            for (size_t i = 0; i < requests_per_client; ++i) {
                auto t0 = std::chrono::high_resolution_clock::now();

                if (::send(fd, raw_resp_cmd.data(), raw_resp_cmd.size(), 0) <= 0) break;

                // Read until newline termination
                while (true) {
                    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    if (n >= 2 && buf[n - 2] == '\r' && buf[n - 1] == '\n') break;
                }

                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                client_latencies[c].push_back(ms);
            }

            ::close(fd);
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(end_time - start_time).count();

    // Flatten latencies
    std::vector<double> all_latencies;
    all_latencies.reserve(total_requests);
    for (auto& cl : client_latencies) {
        all_latencies.insert(all_latencies.end(), cl.begin(), cl.end());
    }

    return compute_stats(all_latencies, total_sec);
}

void run_network_benchmarks(const std::string& host, uint16_t port, size_t total_requests, size_t clients) {
    std::cout << color::BOLD << color::CYAN
              << "\n========================================================================================\n"
              << "       RunDB Native TCP Network Benchmark (" << clients << " Concurrent Clients, "
              << total_requests << " Ops/cmd)      \n"
              << "========================================================================================\n"
              << color::RESET
              << "Target Server: " << host << ":" << port << "\n\n";

    double rss_start = get_process_rss_mb();
    double peak_rss = rss_start;

    struct NetTest {
        std::string name;
        std::string raw_resp;
    };

    std::vector<NetTest> tests = {
        {"PING",  "*1\r\n$4\r\nPING\r\n"},
        {"SET",   "*3\r\n$3\r\nSET\r\n$7\r\nbench_k\r\n$7\r\nbench_v\r\n"},
        {"GET",   "*2\r\n$3\r\nGET\r\n$7\r\nbench_k\r\n"},
        {"INCR",  "*2\r\n$4\r\nINCR\r\n$9\r\nbench_cnt\r\n"},
        {"LPUSH", "*3\r\n$5\r\nLPUSH\r\n$8\r\nbench_li\r\n$4\r\nitem\r\n"},
        {"LPOP",  "*2\r\n$4\r\nLPOP\r\n$8\r\nbench_li\r\n"},
        {"SADD",  "*3\r\n$4\r\nSADD\r\n$8\r\nbench_st\r\n$3\r\n100\r\n"}
    };

    print_benchmark_table_header();

    for (const auto& t : tests) {
        LatencyStats stats = run_tcp_command_benchmark(host, port, t.name, t.raw_resp, total_requests, clients);
        print_benchmark_row(t.name, stats);

        double cur_rss = get_process_rss_mb();
        if (cur_rss > peak_rss) peak_rss = cur_rss;
    }

    print_benchmark_table_footer();
    std::cout << "Memory: Baseline RSS = " << std::fixed << std::setprecision(2) << rss_start
              << " MB | Peak RSS = " << peak_rss
              << " MB | Delta = " << (peak_rss - rss_start) << " MB\n";
}

} // namespace

int main(int argc, char* argv[]) {
    rundb::Logger::init("WARNING");

    bool run_engine = false;
    bool run_network = false;
    size_t requests = 50000;
    size_t clients = 20;
    std::string host = "127.0.0.1";
    uint16_t port = 17379;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--engine") {
            run_engine = true;
        } else if (arg == "--network") {
            run_network = true;
        } else if ((arg == "-n" || arg == "--requests") && i + 1 < argc) {
            requests = std::stoull(argv[++i]);
        } else if ((arg == "-c" || arg == "--clients") && i + 1 < argc) {
            clients = std::stoull(argv[++i]);
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "RunDB Native C++ Benchmarking Suite\n"
                      << "Usage: rundb_benchmark [OPTIONS]\n"
                      << "  --engine           Run in-memory direct engine microbenchmarks\n"
                      << "  --network          Run TCP network benchmark against server\n"
                      << "  -n, --requests <N> Number of operations per command (default: 50000)\n"
                      << "  -c, --clients <C>  Number of concurrent client threads (default: 20)\n"
                      << "  -p, --port <P>     Target port for TCP benchmark (default: 17379)\n"
                      << "  -h, --help         Show this help message\n\n"
                      << "If neither --engine nor --network is specified, both are executed.\n";
            return 0;
        }
    }

    if (!run_engine && !run_network) {
        run_engine = true;
        run_network = true;
    }

    if (run_engine) {
        run_engine_microbenchmarks(requests);
    }

    if (run_network) {
        // Start an embedded server for the network benchmark if port is 17379
        rundb::server::Shutdown::reset();
        rundb::core::Store store;
        rundb::server::Server server(host, port, store);

        bool started_embedded = false;
        std::thread server_thread;

        if (server.init()) {
            started_embedded = true;
            server_thread = std::thread([&]() {
                server.run();
            });
            // Give server a moment to enter epoll_wait
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        run_network_benchmarks(host, port, requests, clients);

        if (started_embedded) {
            rundb::server::Shutdown::request_shutdown();
            if (server_thread.joinable()) {
                server_thread.join();
            }
        }
    }

    std::cout << "\n" << color::GREEN << color::BOLD << "✓ Benchmark run completed successfully!" << color::RESET << "\n\n";

    rundb::Logger::shutdown();
    return 0;
}
