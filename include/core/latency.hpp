#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <sstream>

namespace rundb::core {

struct LatencySample {
    uint64_t timestamp_s{0};
    uint64_t latency_ms{0};
};

struct LatencyEntry {
    std::string name;
    uint64_t max_latency_ms{0};
    std::vector<LatencySample> samples;
};

/**
 * @brief High-performance Latency Monitoring engine for RunDB.
 * Wire-compatible with Redis LATENCY monitoring commands.
 */
class LatencyMonitor {
public:
    static constexpr size_t MAX_SAMPLES_PER_EVENT = 160;

    static LatencyMonitor& instance() noexcept {
        static LatencyMonitor s_instance;
        return s_instance;
    }

    void record_event(const std::string& event, uint64_t latency_ms) {
        if (event.empty()) return;
        auto now_s = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );

        auto& entry = m_events[event];
        entry.name = event;
        if (latency_ms > entry.max_latency_ms) {
            entry.max_latency_ms = latency_ms;
        }

        if (entry.samples.size() >= MAX_SAMPLES_PER_EVENT) {
            entry.samples.erase(entry.samples.begin());
        }
        entry.samples.push_back(LatencySample{now_s, latency_ms});
    }

    struct LatestItem {
        std::string event;
        uint64_t timestamp_s{0};
        uint64_t latest_ms{0};
        uint64_t max_ms{0};
    };

    [[nodiscard]] std::vector<LatestItem> get_latest() const {
        std::vector<LatestItem> result;
        for (const auto& [name, entry] : m_events) {
            if (entry.samples.empty()) continue;
            const auto& last = entry.samples.back();
            result.push_back(LatestItem{
                .event = name,
                .timestamp_s = last.timestamp_s,
                .latest_ms = last.latency_ms,
                .max_ms = entry.max_latency_ms
            });
        }
        return result;
    }

    [[nodiscard]] std::vector<LatencySample> get_history(const std::string& event) const {
        auto it = m_events.find(event);
        if (it == m_events.end()) return {};
        return it->second.samples;
    }

    size_t reset(const std::vector<std::string>& events = {}) {
        if (events.empty()) {
            size_t count = 0;
            for (auto& [k, v] : m_events) {
                if (!v.samples.empty()) {
                    v.samples.clear();
                    v.max_latency_ms = 0;
                    count++;
                }
            }
            return count;
        }

        size_t count = 0;
        for (const auto& ev : events) {
            auto it = m_events.find(ev);
            if (it != m_events.end() && !it->second.samples.empty()) {
                it->second.samples.clear();
                it->second.max_latency_ms = 0;
                count++;
            }
        }
        return count;
    }

    [[nodiscard]] std::string generate_graph(const std::string& event) const {
        auto it = m_events.find(event);
        if (it == m_events.end() || it->second.samples.empty()) {
            return "No samples available for event '" + event + "'\n";
        }

        const auto& samples = it->second.samples;
        uint64_t max_val = it->second.max_latency_ms;
        if (max_val == 0) max_val = 1;

        std::ostringstream oss;
        oss << event << " - high " << max_val << " ms\n";
        oss << "--------------------------------------------------------------------------------\n";

        // Draw 10-line vertical ASCII bar chart
        const int HEIGHT = 10;
        for (int row = HEIGHT; row >= 1; --row) {
            double threshold = (static_cast<double>(row) / HEIGHT) * max_val;
            oss << "|";
            for (const auto& s : samples) {
                if (static_cast<double>(s.latency_ms) >= threshold) {
                    oss << "#";
                } else {
                    oss << " ";
                }
            }
            oss << "\n";
        }

        oss << "--------------------------------------------------------------------------------\n";
        oss << samples.size() << " samples recorded. Max: " << max_val << "ms\n";
        return oss.str();
    }

    [[nodiscard]] std::string generate_doctor() const {
        bool has_samples = false;
        for (const auto& [k, v] : m_events) {
            if (!v.samples.empty()) {
                has_samples = true;
                break;
            }
        }

        if (!has_samples) {
            return "No latency spikes were detected!\n";
        }

        std::ostringstream oss;
        oss << "The following latency events were analyzed:\n\n";
        for (const auto& [name, entry] : m_events) {
            if (entry.samples.empty()) continue;
            uint64_t sum = 0;
            for (const auto& s : entry.samples) sum += s.latency_ms;
            uint64_t avg = sum / entry.samples.size();
            oss << "* Event '" << name << "': max=" << entry.max_latency_ms
                << "ms, avg=" << avg << "ms (" << entry.samples.size() << " samples)\n";
            if (name == "command") {
                oss << "  Advice: Consider reviewing slow queries or using pipelines.\n";
            } else if (name == "aof-write" || name == "aof-fsync") {
                oss << "  Advice: Consider setting 'appendfsync everysec' or checking disk I/O.\n";
            } else if (name == "eviction-cycle") {
                oss << "  Advice: Consider increasing maxmemory or switching eviction policy.\n";
            }
        }
        return oss.str();
    }

    [[nodiscard]] static std::vector<std::string> get_help() {
        return {
            "LATENCY <subcommand> [<arg> [value] ...]. Subcommands are:",
            "DOCTOR",
            "    Return a human readable latency analysis report.",
            "GRAPH <event>",
            "    Produce an ASCII-art graph of latency for the specified event.",
            "HISTORY <event>",
            "    Return time-series latency samples for the specified event.",
            "LATEST",
            "    Return the latest latency samples for all events.",
            "RESET [<event> ...]",
            "    Reset latency data for specified events, or all if none given.",
            "HELP",
            "    Print this help."
        };
    }

private:
    LatencyMonitor() = default;
    std::unordered_map<std::string, LatencyEntry> m_events;
};

} // namespace rundb::core
