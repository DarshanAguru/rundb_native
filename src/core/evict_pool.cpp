#include "core/evict_pool.hpp"
#include "core/object.hpp"
#include <algorithm>

namespace rundb::core {

uint32_t EvictPool::calculate_idle_time(uint32_t obj_lru) noexcept {
    uint32_t current = RunDBObject::get_lru_clock();
    if (current >= obj_lru) {
        return current - obj_lru;
    }
    // 24-bit wrap-around resolution
    return (0x00FFFFFF - obj_lru) + current;
}

void EvictPool::insert(uint32_t idle_time, internals::SDS key, int db_id) {
    // Check if key already in pool
    for (auto& cand : m_pool) {
        if (cand.key == key && cand.db_id == db_id) {
            cand.idle_time = idle_time;
            std::sort(m_pool.begin(), m_pool.end());
            return;
        }
    }

    if (m_pool.size() < POOL_SIZE) {
        m_pool.push_back({idle_time, std::move(key), db_id});
        std::sort(m_pool.begin(), m_pool.end());
    } else if (idle_time > m_pool.front().idle_time) {
        m_pool.front() = {idle_time, std::move(key), db_id};
        std::sort(m_pool.begin(), m_pool.end());
    }
}

void EvictPool::insert(uint32_t idle_time, std::string_view key, int db_id) {
    insert(idle_time, internals::SDS(key), db_id);
}

std::optional<EvictCandidate> EvictPool::pop_best() {
    if (m_pool.empty()) return std::nullopt;
    EvictCandidate best = std::move(m_pool.back());
    m_pool.pop_back();
    return best;
}

void EvictPool::remove_key(std::string_view key, int db_id) {
    m_pool.erase(
        std::remove_if(m_pool.begin(), m_pool.end(), [&](const EvictCandidate& c) {
            return c.key == key && c.db_id == db_id;
        }),
        m_pool.end()
    );
}

} // namespace rundb::core
