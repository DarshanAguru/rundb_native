#include "core/stats.hpp"
#include "core/internals/run_alloc.hpp"
#include <chrono>

namespace rundb::core {

MemoryStats Stats::get_memory_stats(size_t max_memory) const noexcept {
    size_t allocated = internals::run_alloc::get_used_memory();
    size_t resident = internals::run_alloc::get_resident_memory();
    double frag = (allocated > 0) ? (static_cast<double>(resident) / allocated) : 1.0;
    return MemoryStats{
        .used_memory = allocated,
        .resident_memory = resident,
        .max_memory = max_memory,
        .fragmentation_ratio = frag
    };
}

std::vector<DBStats> Stats::get_keyspace_stats(const std::vector<Database>& databases) const {
    std::vector<DBStats> result;
    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    for (const auto& db : databases) {
        if (db.key_count() == 0) continue;

        uint64_t total_ttl = 0;
        size_t active_expires = 0;

        for (const auto& [key, exp_time] : db.expires()) {
            if (exp_time > now_ms) {
                total_ttl += (exp_time - now_ms);
                active_expires++;
            }
        }

        uint64_t avg_ttl = (active_expires > 0) ? (total_ttl / active_expires) : 0;
        result.push_back(DBStats{
            .db_id = db.id(),
            .keys = db.key_count(),
            .expires = active_expires,
            .avg_ttl_ms = avg_ttl
        });
    }

    return result;
}

} // namespace rundb::core
