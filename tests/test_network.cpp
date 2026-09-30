#include "test_framework.hpp"
#include "server/server.hpp"
#include "server/shutdown.hpp"
#include "core/store.hpp"

#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

int connect_to_server(const std::string& host, uint16_t port, int timeout_ms = 2000) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            return fd;
        }

        ::close(fd);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string send_and_recv(int fd, const std::string& cmd) {
    if (::send(fd, cmd.data(), cmd.size(), 0) <= 0) {
        return "";
    }

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
        if (response.size() >= 2 && response.substr(response.size() - 2) == "\r\n") {
            break;
        }
    }
    return response;
}

} // namespace

TEST_CASE("Network_EndToEndPingEcho") {
    rundb::server::Shutdown::reset();
    rundb::core::Store store;
    const uint16_t port = 19371;

    rundb::server::Server server("127.0.0.1", port, store);
    ASSERT_TRUE(server.init());

    std::thread server_thread([&]() {
        server.run();
    });

    int client_fd = connect_to_server("127.0.0.1", port);
    ASSERT_GT(client_fd, 0);

    ASSERT_EQ(send_and_recv(client_fd, "*1\r\n$4\r\nPING\r\n"), "+PONG\r\n");
    ASSERT_EQ(send_and_recv(client_fd, "*2\r\n$4\r\nECHO\r\n$5\r\nHELLO\r\n"), "$5\r\nHELLO\r\n");

    ::close(client_fd);
    rundb::server::Shutdown::request_shutdown();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_CASE("Network_Pipelining") {
    rundb::server::Shutdown::reset();
    rundb::core::Store store;
    const uint16_t port = 19372;

    rundb::server::Server server("127.0.0.1", port, store);
    ASSERT_TRUE(server.init());

    std::thread server_thread([&]() {
        server.run();
    });

    int client_fd = connect_to_server("127.0.0.1", port);
    ASSERT_GT(client_fd, 0);

    // Send 3 pipelined commands in a single buffer
    std::string pipeline =
        "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$3\r\n100\r\n"
        "*2\r\n$4\r\nINCR\r\n$1\r\na\r\n"
        "*2\r\n$3\r\nGET\r\n$1\r\na\r\n";

    ASSERT_GT(::send(client_fd, pipeline.data(), pipeline.size(), 0), 0);

    // Read full reply
    std::string full_reply;
    char buf[4096];
    while (full_reply.find("$3\r\n101\r\n") == std::string::npos) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        full_reply.append(buf, static_cast<size_t>(n));
    }

    ASSERT_EQ(full_reply, "+OK\r\n:101\r\n$3\r\n101\r\n");

    ::close(client_fd);
    rundb::server::Shutdown::request_shutdown();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_CASE("Network_ConcurrentClients") {
    rundb::server::Shutdown::reset();
    rundb::core::Store store;
    const uint16_t port = 19373;

    rundb::server::Server server("127.0.0.1", port, store);
    ASSERT_TRUE(server.init());

    std::thread server_thread([&]() {
        server.run();
    });

    const int num_clients = 8;
    const int ops_per_client = 25;
    std::vector<std::thread> workers;

    for (int c = 0; c < num_clients; ++c) {
        workers.emplace_back([port, c, ops_per_client]() {
            int fd = connect_to_server("127.0.0.1", port);
            if (fd < 0) return;

            for (int i = 0; i < ops_per_client; ++i) {
                std::string k = "client_" + std::to_string(c) + "_key_" + std::to_string(i);
                std::string v = "val_" + std::to_string(i);
                std::string set_cmd = "SET " + k + " " + v + "\r\n";
                std::string set_res = send_and_recv(fd, set_cmd);
                ASSERT_EQ(set_res, "+OK\r\n");

                std::string get_cmd = "GET " + k + "\r\n";
                std::string get_res = send_and_recv(fd, get_cmd);
                ASSERT_EQ(get_res, "$" + std::to_string(v.size()) + "\r\n" + v + "\r\n");
            }

            ::close(fd);
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    rundb::server::Shutdown::request_shutdown();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}
