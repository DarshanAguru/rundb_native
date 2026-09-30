#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace rundb {

    /**
     * @brief Runtime configuration options parsed from CLI flags, config file, and env vars.
     */
    struct Args {
        std::uint16_t port = 7379;                         ///< Listening TCP port (default: 7379)
        std::string host = "127.0.0.1";                    ///< Bind IP address interface (default: 127.0.0.1)
        std::string log_level = "INFO";                    ///< Logging verbosity (TRACE, DEBUG, INFO, WARNING, ERROR)
        std::string config_path = "config/rundb.conf";     ///< Configuration file path
        std::size_t memory_limit = 0;                      ///< Max memory limit in bytes (0 = unlimited)
        std::size_t max_clients = 10000;                   ///< Max concurrent client connections
        double cron_freq_interval = 0.05;                  ///< Maintenance cron interval in seconds
        std::string aof_file = "appendonly.aof";           ///< AOF log file path
        bool aof_enabled = false;                          ///< Enable AOF persistence
        std::string aof_fsync = "everysec";                ///< Fsync policy: always, everysec, no
        std::string eviction_strategy = "noeviction";      ///< Eviction strategy: allkeys-lru, volatile-lru, etc.
        double eviction_ratio = 0.1;                       ///< Eviction target ratio
        std::size_t db_count = 16;                         ///< Number of database partitions
        std::size_t eviction_pool_size = 16;               ///< LRU candidate eviction pool size
        std::size_t eviction_sample_size = 5;              ///< Samples picked per eviction pass

        // CLI explicit flag markers (for strict CLI > Env > Conf precedence)
        bool has_port{false};
        bool has_host{false};
        bool has_log_level{false};
        bool has_config_path{false};
        bool has_memory_limit{false};
        bool has_max_clients{false};
        bool has_cron_freq_interval{false};
        bool has_aof_file{false};
        bool has_aof_enabled{false};
        bool has_aof_fsync{false};
        bool has_eviction_strategy{false};
        bool has_eviction_ratio{false};
        bool has_db_count{false};
        bool has_eviction_pool_size{false};
        bool has_eviction_sample_size{false};
    };

    /**
     * @brief Command line argument parser for RunDB.
     */
    class ArgParse {
        public:
            /**
             * @brief Parses argc and argv command line arguments.
             * @throws std::runtime_error on invalid arguments or out-of-range values.
             */
            static Args parse(int argc, char* argv[]);

            /**
             * @brief Displays CLI usage information and exits.
             */
            static void print_help();
    };

} // namespace rundb