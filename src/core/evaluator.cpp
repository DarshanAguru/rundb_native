#include "core/evaluator.hpp"
#include "core/aof.hpp"
#include "protocol/resp.hpp"
#include "util/printer.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>

namespace rundb::core {

using protocol::RespParser;

static std::string to_upper(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
        s.push_back(c);
    }
    return s;
}

std::string Evaluator::evaluate(Store& store, ClientContext& ctx, const std::vector<std::string>& tokens) {
    if (tokens.empty()) return RespParser::encode_error("ERR empty command");

    std::string cmd = tokens[0];
    for (char& c : cmd) {
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    }

    // Handle Transaction commands (MULTI, EXEC, DISCARD)
    if (cmd == "MULTI" || cmd == "EXEC" || cmd == "DISCARD") {
        return eval_transaction(store, ctx, cmd, tokens);
    }

    // If client is inside MULTI transaction, queue the command
    if (ctx.in_transaction) {
        ctx.tx_queue.push_back(tokens);
        return "+QUEUED\r\n";
    }

    // Database partition for active connection
    Database& db = store.get_db(ctx.active_db);

    // Routing by command name
    if (cmd == "SET" || cmd == "GET" || cmd == "MSET" || cmd == "MGET" ||
        cmd == "INCR" || cmd == "DECR" || cmd == "INCRBY" || cmd == "DECRBY" ||
        cmd == "APPEND" || cmd == "STRLEN") {
        return eval_strings(store, db, cmd, tokens);
    }

    if (cmd == "LPUSH" || cmd == "RPUSH" || cmd == "LPOP" || cmd == "RPOP" ||
        cmd == "LLEN" || cmd == "LINDEX" || cmd == "LRANGE") {
        return eval_lists(store, db, cmd, tokens);
    }

    if (cmd == "SADD" || cmd == "SREM" || cmd == "SISMEMBER" || cmd == "SCARD" ||
        cmd == "SMEMBERS" || cmd == "SRANDMEMBER") {
        return eval_sets(store, db, cmd, tokens);
    }

    if (cmd == "DEL" || cmd == "EXISTS" || cmd == "TYPE" || cmd == "EXPIRE" ||
        cmd == "PEXPIRE" || cmd == "EXPIREAT" || cmd == "PEXPIREAT" ||
        cmd == "TTL" || cmd == "PTTL" || cmd == "PERSIST" || cmd == "KEYS" ||
        cmd == "FLUSHDB" || cmd == "FLUSHALL") {
        return eval_generic(store, db, cmd, tokens);
    }

    if (cmd == "PING" || cmd == "ECHO" || cmd == "SELECT" || cmd == "INFO" ||
        cmd == "CONFIG" || cmd == "COMMAND" || cmd == "TIME" || cmd == "DBSIZE" ||
        cmd == "BGREWRITEAOF" || cmd == "CLIENT" || cmd == "LATENCY") {
        return eval_admin(store, ctx, db, cmd, tokens);
    }

    return RespParser::encode_error("ERR unknown command '" + tokens[0] + "'");
}

std::string Evaluator::eval_strings(Store& /*store*/, Database& db, const std::string& cmd, const std::vector<std::string>& tokens) {
    if (cmd == "SET") {
        if (tokens.size() < 3) return RespParser::encode_error("ERR wrong number of arguments for 'set' command");
        const std::string& key = tokens[1];
        const std::string& val = tokens[2];

        // Fast path for standard SET key val (skip option parsing & existence check)
        if (tokens.size() == 3) {
            db.set(key, RunDBObject::create_string(val));
            return RespParser::encode_ok();
        }

        uint64_t expire_at_ms = 0;
        bool nx = false;
        bool xx = false;

        for (size_t i = 3; i < tokens.size(); ++i) {
            std::string opt = to_upper(tokens[i]);
            if (opt == "EX" && i + 1 < tokens.size()) {
                int64_t sec = std::stoll(tokens[++i]);
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                expire_at_ms = static_cast<uint64_t>(now + (sec * 1000));
            } else if (opt == "PX" && i + 1 < tokens.size()) {
                int64_t ms = std::stoll(tokens[++i]);
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                expire_at_ms = static_cast<uint64_t>(now + ms);
            } else if (opt == "NX") {
                nx = true;
            } else if (opt == "XX") {
                xx = true;
            }
        }

        bool key_exists = db.exists(key);
        if (nx && key_exists) return RespParser::encode_nil();
        if (xx && !key_exists) return RespParser::encode_nil();

        db.set(key, RunDBObject::create_string(val));
        if (expire_at_ms > 0) {
            db.set_expire(key, expire_at_ms);
        }
        return RespParser::encode_ok();
    }

    if (cmd == "GET") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'get' command");
        auto obj = db.get(tokens[1]);
        if (!obj) {
            Stats::instance().record_miss();
            return RespParser::encode_nil();
        }
        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        Stats::instance().record_hit();
        if (obj->encoding() == ObjectEncoding::InlineInt) {
            return RespParser::encode_bulk_string(std::to_string(obj->get_int_value()));
        }
        return RespParser::encode_bulk_string(obj->get_string_view());
    }

    if (cmd == "MSET") {
        if (tokens.size() < 3 || (tokens.size() % 2) == 0) {
            return RespParser::encode_error("ERR wrong number of arguments for 'mset' command");
        }
        for (size_t i = 1; i < tokens.size(); i += 2) {
            db.set(tokens[i], RunDBObject::create_string(tokens[i + 1]));
        }
        return RespParser::encode_ok();
    }

    if (cmd == "MGET") {
        if (tokens.size() < 2) return RespParser::encode_error("ERR wrong number of arguments for 'mget' command");
        std::string out = "*" + std::to_string(tokens.size() - 1) + "\r\n";
        for (size_t i = 1; i < tokens.size(); ++i) {
            auto obj = db.get(tokens[i]);
            if (!obj || obj->type() != ObjectType::String) {
                out += "$-1\r\n";
                Stats::instance().record_miss();
            } else {
                Stats::instance().record_hit();
                out += RespParser::encode_bulk_string(obj->get_string_value());
            }
        }
        return out;
    }

    if (cmd == "INCR" || cmd == "DECR" || cmd == "INCRBY" || cmd == "DECRBY") {
        if ((cmd == "INCR" || cmd == "DECR") && tokens.size() != 2) {
            return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        }
        if ((cmd == "INCRBY" || cmd == "DECRBY") && tokens.size() != 3) {
            return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        }

        int64_t delta = 1;
        if (cmd == "DECR") delta = -1;
        if (cmd == "INCRBY") delta = std::stoll(tokens[2]);
        if (cmd == "DECRBY") delta = -std::stoll(tokens[2]);

        const std::string& key = tokens[1];
        auto obj = db.get(key);
        if (!obj) {
            db.set(key, RunDBObject::create_int(delta));
            return RespParser::encode_integer(delta);
        }

        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        int64_t cur = obj->get_int_value();
        cur += delta;
        db.set(key, RunDBObject::create_int(cur));
        return RespParser::encode_integer(cur);
    }

    if (cmd == "APPEND") {
        if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'append' command");
        const std::string& key = tokens[1];
        const std::string& suffix = tokens[2];

        auto obj = db.get(key);
        if (!obj) {
            db.set(key, RunDBObject::create_string(suffix));
            return RespParser::encode_integer(static_cast<int64_t>(suffix.size()));
        }

        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        std::string val = obj->get_string_value() + suffix;
        size_t len = val.size();
        db.set(key, RunDBObject::create_string(val));
        return RespParser::encode_integer(static_cast<int64_t>(len));
    }

    if (cmd == "STRLEN") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'strlen' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        return RespParser::encode_integer(static_cast<int64_t>(obj->get_string_value().size()));
    }

    return RespParser::encode_error("ERR unknown string command");
}

std::string Evaluator::eval_lists(Store& /*store*/, Database& db, const std::string& cmd, const std::vector<std::string>& tokens) {
    if (cmd == "LPUSH" || cmd == "RPUSH") {
        if (tokens.size() < 3) return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        const std::string& key = tokens[1];
        auto obj = db.get(key);

        if (!obj) {
            obj = RunDBObject::create_list();
            db.set(key, obj);
        } else if (obj->type() != ObjectType::List) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        auto* ql = obj->get_list();
        if (cmd == "LPUSH") {
            for (size_t i = 2; i < tokens.size(); ++i) {
                ql->push_front(tokens[i]);
            }
        } else {
            for (size_t i = 2; i < tokens.size(); ++i) {
                ql->push_back(tokens[i]);
            }
        }
        return RespParser::encode_integer(static_cast<int64_t>(ql->size()));
    }

    if (cmd == "LPOP" || cmd == "RPOP") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        const std::string& key = tokens[1];
        auto obj = db.get(key);
        if (!obj) return RespParser::encode_nil();
        if (obj->type() != ObjectType::List) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        auto* ql = obj->get_list();
        auto popped = (cmd == "LPOP") ? ql->pop_front() : ql->pop_back();
        if (!popped.has_value()) return RespParser::encode_nil();
        if (ql->empty()) db.del(key);
        return RespParser::encode_bulk_string(*popped);
    }

    if (cmd == "LLEN") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'llen' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::List) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        return RespParser::encode_integer(static_cast<int64_t>(obj->get_list()->size()));
    }

    if (cmd == "LINDEX") {
        if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'lindex' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_nil();
        if (obj->type() != ObjectType::List) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        int64_t idx = std::stoll(tokens[2]);
        auto elem = obj->get_list()->at(idx);
        if (!elem.has_value()) return RespParser::encode_nil();
        return RespParser::encode_bulk_string(*elem);
    }

    if (cmd == "LRANGE") {
        if (tokens.size() != 4) return RespParser::encode_error("ERR wrong number of arguments for 'lrange' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return "*0\r\n";
        if (obj->type() != ObjectType::List) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        int64_t start = std::stoll(tokens[2]);
        int64_t stop = std::stoll(tokens[3]);
        auto items = obj->get_list()->range(start, stop);
        return RespParser::encode_array(items);
    }

    return RespParser::encode_error("ERR unknown list command");
}

std::string Evaluator::eval_sets(Store& /*store*/, Database& db, const std::string& cmd, const std::vector<std::string>& tokens) {
    if (cmd == "SADD") {
        if (tokens.size() < 3) return RespParser::encode_error("ERR wrong number of arguments for 'sadd' command");
        const std::string& key = tokens[1];
        auto obj = db.get(key);

        if (!obj) {
            obj = RunDBObject::create_set();
            db.set(key, obj);
        } else if (obj->type() != ObjectType::Set) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        int64_t added = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (obj->set_add(tokens[i])) {
                added++;
            }
        }
        return RespParser::encode_integer(added);
    }

    if (cmd == "SREM") {
        if (tokens.size() < 3) return RespParser::encode_error("ERR wrong number of arguments for 'srem' command");
        const std::string& key = tokens[1];
        auto obj = db.get(key);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::Set) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        int64_t removed = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (obj->set_remove(tokens[i])) {
                removed++;
            }
        }
        if (obj->set_size() == 0) db.del(key);
        return RespParser::encode_integer(removed);
    }

    if (cmd == "SISMEMBER") {
        if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'sismember' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::Set) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        return RespParser::encode_integer(obj->set_contains(tokens[2]) ? 1 : 0);
    }

    if (cmd == "SCARD") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'scard' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::Set) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        return RespParser::encode_integer(static_cast<int64_t>(obj->set_size()));
    }

    if (cmd == "SMEMBERS") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'smembers' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return "*0\r\n";
        if (obj->type() != ObjectType::Set) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        std::vector<std::string> members;
        if (obj->encoding() == ObjectEncoding::IntSet) {
            for (int64_t v : obj->get_intset()->to_vector()) {
                members.push_back(std::to_string(v));
            }
        } else if (auto* hs = obj->get_hashset()) {
            members.assign(hs->begin(), hs->end());
        }
        return RespParser::encode_array(members);
    }

    return RespParser::encode_error("ERR unknown set command");
}

std::string Evaluator::eval_generic(Store& store, Database& db, const std::string& cmd, const std::vector<std::string>& tokens) {
    if (cmd == "DEL") {
        if (tokens.size() < 2) return RespParser::encode_error("ERR wrong number of arguments for 'del' command");
        int64_t deleted = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (db.del(tokens[i])) deleted++;
        }
        return RespParser::encode_integer(deleted);
    }

    if (cmd == "EXISTS") {
        if (tokens.size() < 2) return RespParser::encode_error("ERR wrong number of arguments for 'exists' command");
        int64_t count = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (db.exists(tokens[i])) count++;
        }
        return RespParser::encode_integer(count);
    }

    if (cmd == "TYPE") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'type' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_simple_string("none");
        switch (obj->type()) {
            case ObjectType::String: return RespParser::encode_simple_string("string");
            case ObjectType::List: return RespParser::encode_simple_string("list");
            case ObjectType::Set: return RespParser::encode_simple_string("set");
            case ObjectType::ZSet: return RespParser::encode_simple_string("zset");
            case ObjectType::Hash: return RespParser::encode_simple_string("hash");
        }
        return RespParser::encode_simple_string("none");
    }

    if (cmd == "EXPIRE" || cmd == "PEXPIRE") {
        if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        int64_t val = std::stoll(tokens[2]);
        uint64_t ttl_ms = (cmd == "EXPIRE") ? (val * 1000) : val;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        bool ok = db.set_expire(tokens[1], static_cast<uint64_t>(now + ttl_ms));
        return RespParser::encode_integer(ok ? 1 : 0);
    }

    if (cmd == "TTL" || cmd == "PTTL") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        int64_t ttl_ms = db.get_ttl_ms(tokens[1]);
        if (ttl_ms < 0) return RespParser::encode_integer(ttl_ms);
        int64_t val = (cmd == "TTL") ? ((ttl_ms + 999) / 1000) : ttl_ms;
        return RespParser::encode_integer(val);
    }

    if (cmd == "PERSIST") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'persist' command");
        return RespParser::encode_integer(db.persist(tokens[1]) ? 1 : 0);
    }

    if (cmd == "FLUSHDB") {
        db.flush();
        return RespParser::encode_ok();
    }

    if (cmd == "FLUSHALL") {
        for (auto& database : store.databases()) {
            database.flush();
        }
        return RespParser::encode_ok();
    }

    return RespParser::encode_error("ERR unknown generic command");
}

std::string Evaluator::eval_admin(Store& store, ClientContext& ctx, Database& db, const std::string& cmd, const std::vector<std::string>& tokens) {
    if (cmd == "PING") {
        if (tokens.size() == 1) return RespParser::encode_pong();
        return RespParser::encode_bulk_string(tokens[1]);
    }

    if (cmd == "ECHO") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'echo' command");
        return RespParser::encode_bulk_string(tokens[1]);
    }

    if (cmd == "SELECT") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'select' command");
        int db_id = std::stoi(tokens[1]);
        if (db_id < 0 || static_cast<size_t>(db_id) >= store.db_count()) {
            return RespParser::encode_error("ERR DB index is out of range");
        }
        ctx.active_db = db_id;
        return RespParser::encode_ok();
    }

    if (cmd == "DBSIZE") {
        return RespParser::encode_integer(static_cast<int64_t>(db.key_count()));
    }

    if (cmd == "TIME") {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(now).count() % 1000000;
        return RespParser::encode_array({std::to_string(sec), std::to_string(usec)});
    }

    if (cmd == "COMMAND") {
        return "*0\r\n";
    }

    if (cmd == "INFO") {
        auto mem = Stats::instance().get_memory_stats(store.get_maxmemory());
        std::string info;
        info += "# Server\r\n";
        info += "rundb_version:0.1.0\r\n";
        info += "os:Linux\r\n";
        info += "arch_bits:64\r\n";
        info += "multiplexing_api:epoll\r\n";

        info += "# Memory\r\n";
        info += "used_memory:" + std::to_string(mem.used_memory) + "\r\n";
        info += "used_memory_human:" + util::Printer::format_bytes(mem.used_memory) + "\r\n";
        info += "used_memory_rss:" + std::to_string(mem.resident_memory) + "\r\n";
        info += "used_memory_rss_human:" + util::Printer::format_bytes(mem.resident_memory) + "\r\n";
        info += "maxmemory:" + std::to_string(mem.max_memory) + "\r\n";
        info += "maxmemory_human:" + util::Printer::format_bytes(mem.max_memory) + "\r\n";
        info += "used_memory_pct:" + util::Printer::format_percentage(mem.used_memory, mem.max_memory) + "\r\n";
        info += "mem_allocator:jemalloc\r\n";
        info += "mem_fragmentation_ratio:" + std::to_string(mem.fragmentation_ratio) + "\r\n";

        info += "# Stats\r\n";
        info += "total_connections_received:" + std::to_string(Stats::instance().total_connections()) + "\r\n";
        info += "total_commands_processed:" + std::to_string(Stats::instance().total_commands()) + "\r\n";
        info += "keyspace_hits:" + std::to_string(Stats::instance().keyspace_hits()) + "\r\n";
        info += "keyspace_misses:" + std::to_string(Stats::instance().keyspace_misses()) + "\r\n";

        info += "# Keyspace\r\n";
        for (const auto& ks : Stats::instance().get_keyspace_stats(store.databases())) {
            info += "db" + std::to_string(ks.db_id) + ":keys=" + std::to_string(ks.keys) +
                    ",expires=" + std::to_string(ks.expires) +
                    ",avg_ttl=" + std::to_string(ks.avg_ttl_ms) + "\r\n";
        }

        return RespParser::encode_bulk_string(info);
    }

    if (cmd == "BGREWRITEAOF") {
        if (auto aof = store.get_aof()) {
            if (aof->dump_all(store)) {
                return RespParser::encode_simple_string("Background append only file rewriting started");
            }
            return RespParser::encode_error("ERR Failed to rewrite AOF file");
        }
        return RespParser::encode_error("ERR AOF persistence is not enabled");
    }

    if (cmd == "CLIENT") {
        return RespParser::encode_ok();
    }

    if (cmd == "CONFIG") {
        if (tokens.size() >= 2 && to_upper(tokens[1]) == "GET") {
            std::string param = tokens.size() >= 3 ? tokens[2] : "";
            std::vector<std::string> resp = {param, ""};
            return RespParser::encode_array(resp);
        }
        return RespParser::encode_ok();
    }

    if (cmd == "COMMAND") {
        return "*0\r\n";
    }

    if (cmd == "LATENCY") {
        return "*0\r\n";
    }

    return RespParser::encode_error("ERR unknown admin command");
}

std::string Evaluator::eval_transaction(Store& store, ClientContext& ctx, const std::string& cmd, const std::vector<std::string>& /*tokens*/) {
    if (cmd == "MULTI") {
        if (ctx.in_transaction) {
            return RespParser::encode_error("ERR MULTI calls cannot be nested");
        }
        ctx.in_transaction = true;
        ctx.tx_queue.clear();
        return RespParser::encode_ok();
    }

    if (cmd == "DISCARD") {
        if (!ctx.in_transaction) {
            return RespParser::encode_error("ERR DISCARD without MULTI");
        }
        ctx.reset_transaction();
        return RespParser::encode_ok();
    }

    if (cmd == "EXEC") {
        if (!ctx.in_transaction) {
            return RespParser::encode_error("ERR EXEC without MULTI");
        }

        auto queued = std::move(ctx.tx_queue);
        ctx.reset_transaction();

        std::string reply = "*" + std::to_string(queued.size()) + "\r\n";
        for (const auto& sub_tokens : queued) {
            reply += evaluate(store, ctx, sub_tokens);
        }
        return reply;
    }

    return RespParser::encode_error("ERR unknown transaction command");
}

} // namespace rundb::core
