#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "core/internals/sds.hpp"

namespace rundb::core {

struct EvictCandidate {
    uint32_t idle_time{0}; // Idle seconds computed from 24-bit circular clock
    internals::SDS key;
    int db_id{0};

    bool operator<(const EvictCandidate& other) const noexcept {
        return idle_time < other.idle_time;
    }
};

/**
 * @brief Proprietary Bounded Candidate Eviction Pool.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Bounded Memory & O(1) Overhead:
 *    - Maintains a fixed-size pool of 16 candidates sorted by idle time.
 * 2. Approximated Global LRU:
 *    - Instead of maintaining a full doubly-linked list of millions of keys
 *      (which costs 16 to 24 bytes of pointers per key!), RunDB samples random
 *      keys into this candidate pool on demand.
 * 3. 24-Bit Circular Clock Resolution:
 *    - Accurately tracks idle time across circular counter wrap-arounds.
 */
class EvictPool {
public:
    static constexpr size_t POOL_SIZE = 16;

    EvictPool() = default;
    ~EvictPool() = default;

    void insert(uint32_t idle_time, internals::SDS key, int db_id);
    void insert(uint32_t idle_time, std::string_view key, int db_id);
    std::optional<EvictCandidate> pop_best();
    void remove_key(std::string_view key, int db_id);
    void clear() noexcept { m_pool.clear(); }
    [[nodiscard]] size_t size() const noexcept { return m_pool.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_pool.empty(); }

    [[nodiscard]] static uint32_t calculate_idle_time(uint32_t obj_lru) noexcept;

private:
    std::vector<EvictCandidate> m_pool;
};

} // namespace rundb::core
