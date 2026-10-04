#include "logger.hpp"

#include <cstdlib>
#include <string_view>
#include <utility>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/core/PatternFormatterOptions.h>
#include <quill/sinks/ConsoleSink.h>

namespace rundb {

    quill::Logger* Logger::logger = nullptr;

    /**
     * @brief Starts the Quill backend logging worker thread and configures the console sink.
     * Respects RUNDB_LOG_LEVEL environment variable.
     */
    void Logger::init(const std::string& level_param) {
        // Start asynchronous Quill background thread
        quill::Backend::start();

        std::string level_str = level_param;
        if (level_str.empty()) {
            const char* env_level = std::getenv("RUNDB_LOG_LEVEL");
            if (env_level != nullptr) {
                level_str = env_level;
            }
        }

        quill::LogLevel log_level = quill::LogLevel::Info;

        for (char& c : level_str) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (!level_str.empty()) {
            std::string_view value(level_str);
            if (value == "TRACE") {
                log_level = quill::LogLevel::TraceL1;
            } else if (value == "DEBUG") {
                log_level = quill::LogLevel::Debug;
            } else if (value == "INFO") {
                log_level = quill::LogLevel::Info;
            } else if (value == "WARN" || value == "WARNING") {
                log_level = quill::LogLevel::Warning;
            } else if (value == "ERROR" || value == "ERR") {
                log_level = quill::LogLevel::Error;
            }
        }

        // Configure formatted console sink
        auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
        quill::PatternFormatterOptions formatter_options{
            "%(time) [%(thread_id)] %(short_source_location) %(log_level) "
            "%(logger) %(tags) %(message)"
        };

        // Create thread-safe frontend logger
        logger = quill::Frontend::create_or_get_logger("rundb_logger", std::move(console_sink), formatter_options);
        logger->set_log_level(log_level);
    }

    /**
     * @brief Drains all queues and stops backend logging.
     */
    void Logger::shutdown() {
        quill::Backend::stop();
        logger = nullptr;
    }

    quill::Logger* Logger::get_logger() {
        return logger;
    }

} // namespace rundb
