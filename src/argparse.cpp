#include "argparse.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace rundb {

void ArgParse::print_help() {
    std::cout << R"(
RunDB Native - Redis-inspired high-performance in-memory database (C++20)

USAGE:
    rundb [OPTIONS]

OPTIONS:
    -h, --help
        Show this help message and exit.

    --port <PORT>
        Port on which RunDB will listen. (Default: 7379)

    --host <HOST>
        Network interface / host address to bind to. (Default: 127.0.0.1)

    --log-level <LEVEL>
        Logging verbosity (TRACE, DEBUG, INFO, WARNING, ERROR). (Default: INFO)

    --config <PATH>
        Path to configuration file. (Default: config/rundb.conf)

    --memory-limit <BYTES>
        Max memory limit in bytes, 0 for unlimited. (Default: 0)

    --max-clients <COUNT>
        Max concurrent client connections. (Default: 10000)

    --cron-freq-interval <SECS>
        Maintenance interval in seconds. (Default: 0.05)

    --aof-file <PATH>
        Append-Only File path. (Default: appendonly.aof)

    --aof-enabled [yes|no]
        Enable Append-Only File persistence. The flag alone is sufficient. (Default: no)

    --aof-fsync <always|everysec|no>
        AOF fsync policy. (Default: everysec)

    --eviction-strategy <STRATEGY>
        Eviction strategy (noeviction, allkeys-lru, volatile-lru, allkeys-random, volatile-random).

    --eviction-ratio <RATIO>
        Target ratio for eviction passes. (Default: 0.1)

    --db-count <COUNT>
        Number of isolated database partitions. (Default: 16)

    --eviction-pool-size <SIZE>
        Eviction pool candidate size. (Default: 16)

    --eviction-sample-size <SIZE>
        Keys sampled per eviction pass. (Default: 5)

EXAMPLES:
    rundb --port 6379
    rundb --host 0.0.0.0 --port 7379 --memory-limit 104857600
    rundb --aof-enabled --aof-file appendonly.aof

)";
}

Args ArgParse::parse(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        } else if (arg == "--port" && i + 1 < argc) {
            int port = std::stoi(argv[++i]);
            if (port < 1 || port > 65535) {
                throw std::runtime_error("Port must be between 1 and 65535");
            }
            args.port = static_cast<std::uint16_t>(port);
            args.has_port = true;
        } else if (arg == "--host" && i + 1 < argc) {
            args.host = argv[++i];
            args.has_host = true;
        } else if (arg == "--log-level" && i + 1 < argc) {
            args.log_level = argv[++i];
            args.has_log_level = true;
        } else if (arg == "--config" && i + 1 < argc) {
            args.config_path = argv[++i];
            args.has_config_path = true;
        } else if (arg == "--memory-limit" && i + 1 < argc) {
            args.memory_limit = static_cast<size_t>(std::stoull(argv[++i]));
            args.has_memory_limit = true;
        } else if (arg == "--max-clients" && i + 1 < argc) {
            args.max_clients = static_cast<size_t>(std::stoull(argv[++i]));
            args.has_max_clients = true;
        } else if (arg == "--cron-freq-interval" && i + 1 < argc) {
            args.cron_freq_interval = std::stod(argv[++i]);
            args.has_cron_freq_interval = true;
        } else if (arg == "--aof-file" && i + 1 < argc) {
            args.aof_file = argv[++i];
            args.has_aof_file = true;
        } else if (arg == "--aof-enabled") {
            args.has_aof_enabled = true;
            if (i + 1 < argc) {
                std::string next_val = argv[i + 1];
                if (next_val == "yes" || next_val == "true" || next_val == "1") {
                    args.aof_enabled = true;
                    ++i;
                } else if (next_val == "no" || next_val == "false" || next_val == "0") {
                    args.aof_enabled = false;
                    ++i;
                } else {
                    args.aof_enabled = true;
                }
            } else {
                args.aof_enabled = true;
            }
        } else if (arg == "--aof-fsync" && i + 1 < argc) {
            args.aof_fsync = argv[++i];
            args.has_aof_fsync = true;
        } else if (arg == "--eviction-strategy" && i + 1 < argc) {
            args.eviction_strategy = argv[++i];
            args.has_eviction_strategy = true;
        } else if (arg == "--eviction-ratio" && i + 1 < argc) {
            args.eviction_ratio = std::stod(argv[++i]);
            args.has_eviction_ratio = true;
        } else if (arg == "--db-count" && i + 1 < argc) {
            args.db_count = static_cast<size_t>(std::stoull(argv[++i]));
            args.has_db_count = true;
        } else if (arg == "--eviction-pool-size" && i + 1 < argc) {
            args.eviction_pool_size = static_cast<size_t>(std::stoull(argv[++i]));
            args.has_eviction_pool_size = true;
        } else if (arg == "--eviction-sample-size" && i + 1 < argc) {
            args.eviction_sample_size = static_cast<size_t>(std::stoull(argv[++i]));
            args.has_eviction_sample_size = true;
        } else {
            throw std::runtime_error("Unknown command-line argument: " + arg);
        }
    }

    return args;
}

} // namespace rundb