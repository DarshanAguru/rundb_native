#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include "core/database.hpp"
#include "core/evict_pool.hpp"
#include "core/eviction.hpp"
#include "core/expiration.hpp"
#include "core/stats.hpp"
#include "core/client_context.hpp"

namespace rundb::core {

class AOF;

/**
 * @brief Multi-database storage coordinator managing 16 isolated keyspace partitions,
 *        eviction cycles, background active expiration, and command routing.
 */
class Store {
public:
    static constexpr size_t DEFAULT_DB_COUNT = 16;

    explicit Store(
        size_t maxmemory = 0,
        EvictionPolicy policy = EvictionPolicy::NoEviction,
        size_t db_count = DEFAULT_DB_COUNT
    );
    ~Store();

    // Database partition access
    [[nodiscard]] Database& get_db(int db_id);
    [[nodiscard]] const Database& get_db(int db_id) const;
    [[nodiscard]] size_t db_count() const noexcept { return m_databases.size(); }
    [[nodiscard]] std::vector<Database>& databases() noexcept { return m_databases; }

    // Command dispatch
    std::string process_command(ClientContext& ctx, const std::vector<std::string>& tokens);

    // Maintenance tasks (called periodically by server cron)
    size_t active_expire_cycle();
    size_t check_and_evict() {
        if (__builtin_expect(m_maxmemory == 0 || m_policy == EvictionPolicy::NoEviction, 1)) {
            return 0;
        }
        return perform_eviction_slow();
    }
    size_t perform_eviction_slow();

    // Configuration & persistence
    void set_maxmemory(size_t maxmem) noexcept { m_maxmemory = maxmem; }
    [[nodiscard]] size_t get_maxmemory() const noexcept { return m_maxmemory; }
    void set_eviction_policy(EvictionPolicy policy) noexcept { m_policy = policy; }
    [[nodiscard]] EvictionPolicy get_eviction_policy() const noexcept { return m_policy; }

    void attach_aof(std::shared_ptr<AOF> aof) { m_aof = std::move(aof); }
    void detach_aof() noexcept { m_aof.reset(); }
    [[nodiscard]] std::shared_ptr<AOF> get_aof() const noexcept { return m_aof; }

private:
    std::vector<Database> m_databases;
    EvictPool m_evict_pool;
    size_t m_maxmemory{0};
    EvictionPolicy m_policy{EvictionPolicy::NoEviction};
    std::shared_ptr<AOF> m_aof;
};

} // namespace rundb::core
