#include "core/eviction.hpp"
#include "core/internals/run_alloc.hpp"
#include <algorithm>

namespace rundb::core {

EvictionPolicy Eviction::parse_policy(std::string_view str) {
    if (str == "allkeys-lru") return EvictionPolicy::AllKeysLRU;
    if (str == "volatile-lru") return EvictionPolicy::VolatileLRU;
    if (str == "allkeys-random") return EvictionPolicy::AllKeysRandom;
    if (str == "volatile-random") return EvictionPolicy::VolatileRandom;
    return EvictionPolicy::NoEviction;
}

std::string Eviction::policy_to_string(EvictionPolicy policy) {
    switch (policy) {
        case EvictionPolicy::AllKeysLRU: return "allkeys-lru";
        case EvictionPolicy::VolatileLRU: return "volatile-lru";
        case EvictionPolicy::AllKeysRandom: return "allkeys-random";
        case EvictionPolicy::VolatileRandom: return "volatile-random";
        case EvictionPolicy::NoEviction: default: return "noeviction";
    }
}

size_t Eviction::get_used_memory() noexcept {
    return internals::run_alloc::get_used_memory();
}

bool Eviction::is_memory_exceeded(size_t maxmemory) noexcept {
    if (maxmemory == 0) return false;
    return get_used_memory() > maxmemory;
}

size_t Eviction::perform_eviction(
    std::vector<Database>& databases,
    EvictPool& pool,
    EvictionPolicy policy,
    size_t maxmemory
) {
    if (policy == EvictionPolicy::NoEviction || maxmemory == 0) return 0;
    if (!is_memory_exceeded(maxmemory)) return 0;

    size_t evicted_count = 0;
    bool is_volatile = (policy == EvictionPolicy::VolatileLRU || policy == EvictionPolicy::VolatileRandom);
    bool is_random = (policy == EvictionPolicy::AllKeysRandom || policy == EvictionPolicy::VolatileRandom);

    // Evict loop: evict until memory drops below limit or we fail to find keys
    while (is_memory_exceeded(maxmemory)) {
        if (is_random) {
            bool found_any = false;
            for (auto& db : databases) {
                auto keys = is_volatile ? db.sample_expires(5) : db.sample_keys(5);
                if (!keys.empty()) {
                    db.del(keys.front());
                    evicted_count++;
                    found_any = true;
                    break;
                }
            }
            if (!found_any) break;
        } else {
            // LRU: Populate EvictPool by sampling databases
            for (auto& db : databases) {
                auto sampled = is_volatile ? db.sample_expires(5) : db.sample_keys(5);
                for (const auto& key : sampled) {
                    auto obj = db.get_no_touch(key);
                    if (obj) {
                        uint32_t idle = EvictPool::calculate_idle_time(obj->lru());
                        pool.insert(idle, key, db.id());
                    }
                }
            }

            auto cand = pool.pop_best();
            if (!cand.has_value()) {
                break; // No candidates available
            }

            if (cand->db_id >= 0 && static_cast<size_t>(cand->db_id) < databases.size()) {
                databases[cand->db_id].del(cand->key);
                evicted_count++;
            }
        }
    }

    return evicted_count;
}

} // namespace rundb::core
