#include "core/store.hpp"
#include "core/evaluator.hpp"
#include "core/aof.hpp"

namespace rundb::core {

Store::Store(size_t maxmemory, EvictionPolicy policy, size_t db_count)
    : m_maxmemory(maxmemory), m_policy(policy) {
    m_databases.reserve(db_count);
    for (size_t i = 0; i < db_count; ++i) {
        m_databases.emplace_back(static_cast<int>(i));
    }
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

    // If write command and AOF attached, log it
    if (m_aof && m_aof->is_enabled() && !tokens.empty()) {
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
            m_aof->log_command(tokens);
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
