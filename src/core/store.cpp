#include "core/store.hpp"
#include "core/evaluator.hpp"
#include "core/aof.hpp"
#include "core/snapshot.hpp"

namespace rundb::core {

Store::Store(size_t maxmemory, EvictionPolicy policy, size_t db_count)
    : m_maxmemory(maxmemory), m_policy(policy) {
    m_databases.reserve(db_count);
    for (size_t i = 0; i < db_count; ++i) {
        m_databases.emplace_back(static_cast<int>(i));
    }
    m_config.memory_limit = maxmemory;
    m_config.db_count = db_count;
    m_config.eviction_strategy = Eviction::policy_to_string(policy);
}

void Store::set_maxmemory(size_t maxmem) noexcept {
    m_maxmemory = maxmem;
    m_config.memory_limit = maxmem;
}

void Store::set_eviction_policy(EvictionPolicy policy) noexcept {
    m_policy = policy;
    m_config.eviction_strategy = Eviction::policy_to_string(policy);
}

void Store::register_client(ClientContext* ctx) {
    if (!ctx) return;
    for (auto* c : m_clients) {
        if (c == ctx) return;
    }
    m_clients.push_back(ctx);
}

void Store::unregister_client(ClientContext* ctx) {
    if (!ctx) return;
    auto it = std::remove(m_clients.begin(), m_clients.end(), ctx);
    m_clients.erase(it, m_clients.end());
}

bool Store::kill_client(const std::string& target) {
    for (auto* c : m_clients) {
        if (std::to_string(c->id) == target || c->addr == target) {
            c->close_requested = true;
            return true;
        }
    }
    return false;
}

Store::~Store() = default;

Database& Store::get_db(int db_id) {
    if (db_id < 0 || static_cast<size_t>(db_id) >= m_databases.size()) {
        return m_databases[0];
    }
    return m_databases[static_cast<size_t>(db_id)];
}

const Database& Store::get_db(int db_id) const {
    if (db_id < 0 || static_cast<size_t>(db_id) >= m_databases.size()) {
        return m_databases[0];
    }
    return m_databases[static_cast<size_t>(db_id)];
}

std::string Store::process_command(ClientContext& ctx, const std::vector<std::string>& tokens) {
    if (tokens.empty()) return "-ERR empty command\r\n";

    // Track command telemetry
    Stats::instance().record_command();

    // Check memory limit and evict if needed prior to command
    check_and_evict();

    // Evaluate command
    std::string resp = Evaluator::evaluate(*this, ctx, tokens);

    // If write command and AOF or Snapshot attached and not in transaction queue, notify
    if (!ctx.in_transaction && !tokens.empty()) {
        std::string cmd = tokens[0];
        for (char& c : cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        static const std::unordered_set<std::string> WRITE_CMDS = {
            "SET", "MSET", "INCR", "DECR", "INCRBY", "DECRBY", "APPEND",
            "LPUSH", "RPUSH", "LPOP", "RPOP",
            "SADD", "SREM",
            "DEL", "FLUSHDB", "FLUSHALL", "EXPIRE", "PEXPIRE", "EXPIREAT", "PEXPIREAT", "PERSIST",
            "SELECT"
        };

        if (WRITE_CMDS.find(cmd) != WRITE_CMDS.end()) {
            if (m_aof && m_aof->is_enabled()) {
                m_aof->log_command(tokens);
            }
            if (m_snapshot_mgr) {
                m_snapshot_mgr->notify_keyspace_changed();
            }
        }
    }

    return resp;
}

size_t Store::active_expire_cycle() {
    return Expiration::run_active_cycle(m_databases);
}

size_t Store::perform_eviction_slow() {
    return Eviction::perform_eviction(m_databases, m_evict_pool, m_policy, m_maxmemory);
}

} // namespace rundb::core
