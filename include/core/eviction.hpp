#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/database.hpp"
#include "core/evict_pool.hpp"

namespace rundb::core {

enum class EvictionPolicy {
    NoEviction,
    AllKeysLRU,
    VolatileLRU,
    AllKeysRandom,
    VolatileRandom
};

/**
 * @brief Memory threshold evaluator and eviction policy executor.
 */
class Eviction {
public:
    static EvictionPolicy parse_policy(std::string_view str);
    static std::string policy_to_string(EvictionPolicy policy);

    static bool is_memory_exceeded(size_t maxmemory) noexcept;
    static size_t get_used_memory() noexcept;

    /**
     * @brief Performs one eviction step across databases.
     * @return Number of keys evicted.
     */
    static size_t perform_eviction(
        std::vector<Database>& databases,
        EvictPool& pool,
        EvictionPolicy policy,
        size_t maxmemory
    );
};

} // namespace rundb::core
