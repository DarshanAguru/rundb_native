#include "util/printer.hpp"
#include "version.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace rundb {

    std::string Printer::format_bytes(std::size_t bytes, bool verbose) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        if (bytes < 1024) {
            ss << bytes << (verbose ? " Bytes" : "B");
        } else if (bytes < 1024 * 1024) {
            double kb = static_cast<double>(bytes) / 1024.0;
            ss << kb << (verbose ? " KB" : "K");
        } else if (bytes < 1024 * 1024 * 1024) {
            double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
            ss << mb << (verbose ? " MB" : "M");
        } else {
            double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
            ss << gb << (verbose ? " GB" : "G");
        }

        return ss.str();
    }

    std::string Printer::format_percentage(std::size_t used_bytes, std::size_t max_bytes) {
        if (max_bytes == 0) {
            return "N/A (unlimited)";
        }
        double pct = (static_cast<double>(used_bytes) / static_cast<double>(max_bytes)) * 100.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << pct << "%";
        return ss.str();
    }

    void Printer::print_rundb_banner(const std::string& host, std::uint16_t port) {
        std::cout << Colors::CYAN << R"(
    ███████████                         ██████████   ███████████    
    ░░███░░░░░███                       ░░███░░░░███ ░░███░░░░░███   
     ░███    ░███  █████ ████ ████████   ░███   ░░███ ░███    ░███   
     ░██████████  ░░███ ░███ ░░███░░███  ░███    ░███ ░██████████    
     ░███░░░░░███  ░███ ░███  ░███ ░███  ░███    ░███ ░███░░░░░███   
     ░███    ░███  ░███ ░███  ░███ ░███  ░███    ███  ░███    ░███   
     █████   █████ ░░████████ ████ █████ ██████████   ███████████    
    ░░░░░   ░░░░░   ░░░░░░░░ ░░░░ ░░░░░ ░░░░░░░░░░   ░░░░░░░░░░░   
)" << Colors::END;

        std::cout << "        " << Colors::BOLD << "RUNDB: The Redis-inspired NoSQL Key-Value Store (Native C++20)\n"
                  << "        " << Colors::GREEN << "Version " << rundb::VERSION << " - Built with ❤️  by Darshan\n\n" << Colors::END;

        std::string line1_text = "  Server is running at " + host + ":" + std::to_string(port);
        std::string line2_text = "  Try running: redis-cli -p " + std::to_string(port);

        constexpr std::size_t BOX_WIDTH = 45;
        if (line1_text.size() < BOX_WIDTH) line1_text.append(BOX_WIDTH - line1_text.size(), ' ');
        if (line2_text.size() < BOX_WIDTH) line2_text.append(BOX_WIDTH - line2_text.size(), ' ');

        std::cout << "        ┌─────────────────────────────────────────────┐\n";
        std::cout << "        │" << line1_text << "│\n";
        std::cout << "        │" << line2_text << "│\n";
        std::cout << "        └─────────────────────────────────────────────┘\n\n";
    }

    void Printer::print_shutdown_initiated(int signum) {
        std::cout << "\n        " << Colors::YELLOW << "⚠️ SYS" << Colors::END
                  << "  Received signal " << signum << ". Initiating graceful shutdown...\n";
    }

    void Printer::print_shutdown_stopping() {
        std::cout << "        " << Colors::YELLOW << "🛑 SYS" << Colors::END
                  << "  Shutdown requested. Stop accepting new client connections.\n";
        std::cout << "        " << Colors::YELLOW << "🚪 SYS" << Colors::END
                  << "  All client requests processed. Exiting server loop.\n";
    }

    void Printer::print_shutdown_complete(std::size_t used_bytes, std::size_t max_bytes) {
        double pct_used = 0.0;
        if (max_bytes > 0) {
            pct_used = (static_cast<double>(used_bytes) / static_cast<double>(max_bytes)) * 100.0;
        }

        // 1% of capacity utilized = 100 memory calories burnt
        double calories = pct_used * 100.0;

        std::ostringstream ss_cal;
        ss_cal << std::fixed << std::setprecision(2) << calories;

        std::ostringstream ss_pct;
        ss_pct << std::fixed << std::setprecision(2) << pct_used;

        std::cout << "        " << Colors::GREEN << "🏁 SYS" << Colors::END
                  << "  RunDB server shutdown complete.\n";

        std::cout << "        " << Colors::GREEN << "👋 SYS" << Colors::END
                  << "  Bye bye! You burnt " << Colors::BOLD << ss_cal.str() << Colors::END
                  << " memory calories (" << ss_pct.str() << "% of memory capacity used, "
                  << format_bytes(used_bytes) << ") while running!🏃\n";

        std::cout << "        " << Colors::BLUE << "🚀 SYS" << Colors::END
                  << "  See You Soon!!!\n\n";
    }

} // namespace rundb
