#pragma once

#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <string>
#include <string_view>
#include <utility>

namespace rundb {

    /**
     * @brief Logging verbosity levels.
     */
    enum class LogLevel {
        Trace,
        Debug,
        Info,
        Warning,
        Error
    };

    /**
     * @brief High-performance asynchronous logging wrapper around Quill.
     *
     * Quill uses a lock-free queue per thread, deferring all formatting and I/O
     * to a background thread to achieve sub-microsecond logging latency on the hot path.
     */
    class Logger {
        public:
            /**
             * @brief Initializes the background logging engine and console sink.
             * @param level Explicit log level (e.g. "DEBUG", "INFO", "WARN", "ERROR")
             */
            static void init(const std::string& level = "");

            /**
             * @brief Flushes all queued log records and stops the backend thread.
             */
            static void shutdown();

            /**
             * @brief Retrieves the active Quill logger instance pointer.
             */
            static quill::Logger* get_logger();

        private:
            static quill::Logger* logger;
    };

    // Logging macros with tags
    #define TRACE(tags, ...)\
        LOG_TRACE_L1_TAGS(rundb::Logger::get_logger(), TAGS(tags), __VA_ARGS__)

    #define DEBUG(tags, ...)\
        LOG_DEBUG_TAGS(rundb::Logger::get_logger(), TAGS(tags), __VA_ARGS__)

    #define INFO(tags, ...)\
        LOG_INFO_TAGS(rundb::Logger::get_logger(), TAGS(tags), __VA_ARGS__)

    #define WARNING(tags, ...)\
        LOG_WARNING_TAGS(rundb::Logger::get_logger(), TAGS(tags), __VA_ARGS__)

    #define ERROR(tags, ...)\
        LOG_ERROR_TAGS(rundb::Logger::get_logger(), TAGS(tags), __VA_ARGS__)

} // namespace rundb