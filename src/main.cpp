#include "argparse.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "syscheck.hpp"
#include "core/store.hpp"
#include "core/aof.hpp"
#include "server/server.hpp"
#include "util/printer.hpp"

#include <exception>
#include <iostream>

#define MAIN "MAIN"

/**
 * @brief RunDB entry point.
 *
 * Initialization Lifecycle:
 * 1. CLI Argument Parsing: Parse flags (--port, --host, --log-level, --config).
 * 2. Configuration Loading: Ingest config file and synchronize with environment variables.
 * 3. Logging Service: Spin up asynchronous Quill logging backend thread.
 * 4. System Pre-flight Checks: Validate Linux platform and epoll syscall availability.
 * 5. Core Database Initialization: Initialize partitioned Store with jemalloc backing.
 * 6. Server Startup: Launch single-threaded non-blocking epoll network event loop.
 * 7. Clean Teardown: Display memory calories burnt and terminate cleanly upon signal.
 */
int main(int argc, char* argv[]) {
    // 1. Parse command line arguments
    rundb::Args opts;
    try {
        opts = rundb::ArgParse::parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Configuration Error: " << e.what() << "\n";
        return 1;
    }

    // 2. Load configuration file and export environment variables
    try {
        rundb::Config::load(opts);
    } catch (const std::exception& e) {
        std::cerr << "Config File Error: " << e.what() << "\n";
        return 1;
    }

    // 3. Initialize high-performance asynchronous Quill logger
    rundb::Logger::init(opts.log_level);

    // 4. Verify Linux epoll capability
    DEBUG(MAIN, "Executing system pre-flight checks");
    try {
        rundb::SysCheck::init();
    } catch (const std::exception& e) {
        ERROR(MAIN, "Fatal system pre-flight check failure: {}", e.what());
        rundb::Logger::shutdown();
        return 1;
    }
    DEBUG(MAIN, "System pre-flight checks passed");

    // 5. Initialize Core Store & Server with parsed configuration
    try {
        auto policy = rundb::core::Eviction::parse_policy(opts.eviction_strategy);
        rundb::core::Store store(opts.memory_limit, policy, opts.db_count);

        // AOF Persistence Integration
        if (opts.aof_enabled) {
            auto fsync_p = rundb::core::AOF::parse_fsync(opts.aof_fsync);
            auto aof = std::make_shared<rundb::core::AOF>(opts.aof_file, fsync_p);
            // Replay existing AOF state on boot
            aof->load_into(store);
            if (aof->open()) {
                store.attach_aof(aof);
                INFO(MAIN, "AOF persistence active on {}", opts.aof_file);
            }
        }

        rundb::server::Server server(opts.host, opts.port, store);
        if (!server.init()) {
            ERROR(MAIN, "Failed to initialize server listener on {}:{}", opts.host, opts.port);
            rundb::Logger::shutdown();
            return 1;
        }

        server.run();
    } catch (const std::exception& e) {
        ERROR(MAIN, "Fatal runtime error in server event loop: {}", e.what());
        rundb::Logger::shutdown();
        return 1;
    }

    // 6. Final logger flush
    rundb::Logger::shutdown();
    return 0;
}