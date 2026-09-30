#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "core/database.hpp"

namespace rundb::core {

struct DBStats {
    int db_id{0};
    size_t keys{0};
    size_t expires{0};
    uint64_t avg_ttl_ms{0};
};

struct MemoryStats {
    size_t used_memory{0};
    size_t resident_memory{0};
    size_t max_memory{0};
    double fragmentation_ratio{1.0};
};

/**
 * @brief Telemetry, performance counters, and keyspace statistics.
 */
class Stats {
public:
    static Stats& instance() noexcept {
        static Stats s_instance;
        return s_instance;
    }

    void record_connection() noexcept { m_connections++; }
    void record_command() noexcept { m_commands++; }
    void record_hit() noexcept { m_hits++; }
    void record_miss() noexcept { m_misses++; }

    [[nodiscard]] uint64_t total_connections() const noexcept { return m_connections; }
    [[nodiscard]] uint64_t total_commands() const noexcept { return m_commands; }
    [[nodiscard]] uint64_t keyspace_hits() const noexcept { return m_hits; }
    [[nodiscard]] uint64_t keyspace_misses() const noexcept { return m_misses; }

    [[nodiscard]] MemoryStats get_memory_stats(size_t max_memory) const noexcept;
    [[nodiscard]] std::vector<DBStats> get_keyspace_stats(const std::vector<Database>& databases) const;

private:
    Stats() = default;

    uint64_t m_connections{0};
    uint64_t m_commands{0};
    uint64_t m_hits{0};
    uint64_t m_misses{0};
};

} // namespace rundb::core
