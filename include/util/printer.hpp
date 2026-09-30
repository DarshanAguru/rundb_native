#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rundb {

    /**
     * @brief Terminal formatting, ASCII banner rendering, and human-readable memory analytics.
     *
     * Mimics and expands the Python RunDB Printer utility with C++ performance:
     * - Colorized ANSI output for diagnostics and banners.
     * - Dynamic metric scaling (Bytes -> KB -> MB -> GB).
     * - Accurate capacity percentage calculations and "memory calories" accounting.
     */
    class Printer {
        public:
            struct Colors {
                static constexpr const char* HEADER = "\033[95m";
                static constexpr const char* BLUE   = "\033[94m";
                static constexpr const char* CYAN   = "\033[96m";
                static constexpr const char* GREEN  = "\033[92m";
                static constexpr const char* YELLOW = "\033[93m";
                static constexpr const char* RED    = "\033[91m";
                static constexpr const char* END    = "\033[0m";
                static constexpr const char* BOLD   = "\033[1m";
                static constexpr const char* GRAY   = "\033[90m";
            };

            /**
             * @brief Renders the official RunDB ASCII art banner and connection box.
             * @param host IP address string.
             * @param port TCP port.
             */
            static void print_rundb_banner(const std::string& host, std::uint16_t port);

            /**
             * @brief Dynamically formats raw byte counts into human-readable units (B, KB, MB, GB).
             * @param bytes Raw byte quantity.
             * @param verbose If true, appends full unit ("Bytes", "KB", "MB", "GB"); otherwise short ("B", "K", "M", "G").
             */
            static std::string format_bytes(std::size_t bytes, bool verbose = true);

            /**
             * @brief Computes utilization percentage formatted as a percentage string (e.g. "42.50%").
             * Returns "N/A (unlimited)" if max_bytes is 0.
             */
            static std::string format_percentage(std::size_t used_bytes, std::size_t max_bytes);

            /**
             * @brief Prints shutdown notification when an OS signal is caught.
             */
            static void print_shutdown_initiated(int signum);

            /**
             * @brief Prints server event loop exit diagnostics.
             */
            static void print_shutdown_stopping();

            /**
             * @brief Prints friendly completion summary with memory calories burnt.
             * @param used_bytes Total memory allocated at shutdown.
             * @param max_bytes Configured memory limit (0 if unlimited).
             */
            static void print_shutdown_complete(std::size_t used_bytes, std::size_t max_bytes);
    };

    namespace util {
        using Printer = ::rundb::Printer;
    }

} // namespace rundb
