#include "argparse.hpp"
#include "version.hpp"
#include "core/snapshot.hpp"

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

    -v, --version
        Show RunDB version and exit.

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

    --no-aof
        Disable Append-Only File persistence. (Default: AOF is enabled)

    --aof-enabled [yes|no]
        Enable or disable Append-Only File persistence. (Default: yes)

    --aof-fsync <always|everysec|no>
        AOF fsync policy. (Default: everysec)

    --snapshot <TIME> / --snapshot-interval <TIME> / --save <TIME>
        Periodic snapshot interval: e.g. 1M (1 min), 30S (30 sec), 2H (2 hours). (Default: disabled)

    --snapshot-file <PATH> / --dbfilename <PATH>
        Snapshot RDB dump file path. (Default: dump.rdb)

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
    rundb --no-aof --snapshot 1M
    rundb --snapshot-interval 30S --snapshot-file dump.rdb

)";
}

Args ArgParse::parse(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "rundb version " << rundb::VERSION << "\n";
            std::exit(0);
        } else if (arg == "--port" || arg == "-p") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            int port = 0;
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            if (port < 1 || port > 65535) {
                throw std::runtime_error("Port must be between 1 and 65535");
            }
            args.port = static_cast<std::uint16_t>(port);
            args.has_port = true;
        } else if (arg == "--host") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.host = argv[++i];
            args.has_host = true;
        } else if (arg == "--log-level") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.log_level = argv[++i];
            args.has_log_level = true;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.config_path = argv[++i];
            args.has_config_path = true;
        } else if (arg == "--memory-limit") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.memory_limit = static_cast<size_t>(std::stoull(argv[++i]));
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            args.has_memory_limit = true;
        } else if (arg == "--max-clients") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.max_clients = static_cast<size_t>(std::stoull(argv[++i]));
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            args.has_max_clients = true;
        } else if (arg == "--cron-freq-interval") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.cron_freq_interval = std::stod(argv[++i]);
            } catch (...) {
                throw std::runtime_error("Invalid floating point value for option: " + arg);
            }
            args.has_cron_freq_interval = true;
        } else if (arg == "--aof-file") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.aof_file = argv[++i];
            args.has_aof_file = true;
        } else if (arg == "--no-aof") {
            args.aof_enabled = false;
            args.has_aof_enabled = true;
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
        } else if (arg == "--aof-fsync") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.aof_fsync = argv[++i];
            args.has_aof_fsync = true;
        } else if (arg == "--snapshot" || arg == "--snapshot-interval" || arg == "--save") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.snapshot_interval_str = argv[++i];
            args.snapshot_interval_sec = core::SnapshotManager::parse_interval(args.snapshot_interval_str);
            args.has_snapshot_interval = true;
        } else if (arg == "--snapshot-file" || arg == "--dbfilename") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.snapshot_file = argv[++i];
            args.has_snapshot_file = true;
        } else if (arg == "--eviction-strategy") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            args.eviction_strategy = argv[++i];
            args.has_eviction_strategy = true;
        } else if (arg == "--eviction-ratio") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.eviction_ratio = std::stod(argv[++i]);
            } catch (...) {
                throw std::runtime_error("Invalid floating point value for option: " + arg);
            }
            args.has_eviction_ratio = true;
        } else if (arg == "--db-count") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.db_count = static_cast<size_t>(std::stoull(argv[++i]));
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            args.has_db_count = true;
        } else if (arg == "--eviction-pool-size") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.eviction_pool_size = static_cast<size_t>(std::stoull(argv[++i]));
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            args.has_eviction_pool_size = true;
        } else if (arg == "--eviction-sample-size") {
            if (i + 1 >= argc) throw std::runtime_error("Option '" + arg + "' requires an argument");
            try {
                args.eviction_sample_size = static_cast<size_t>(std::stoull(argv[++i]));
            } catch (...) {
                throw std::runtime_error("Invalid integer for option: " + arg);
            }
            args.has_eviction_sample_size = true;
        } else {
            throw std::runtime_error("Unknown command-line argument: " + arg);
        }
    }

    return args;
}

} // namespace rundb