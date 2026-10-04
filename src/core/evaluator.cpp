#include "core/evaluator.hpp"
#include "core/aof.hpp"
#include "core/snapshot.hpp"
#include "core/latency.hpp"
#include "protocol/resp.hpp"
#include "util/printer.hpp"
#include "version.hpp"
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

template <typename T>
static bool parse_int(std::string_view sv, T& out) {
    if (sv.empty()) return false;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc() && ptr == sv.data() + sv.size();
}

struct CommandRule {
    std::string name;
    int arity;
    std::vector<std::string> flags;
    int first_key;
    int last_key;
    int step;
    std::string summary;
};

static const std::vector<CommandRule>& get_command_table() {
    static const std::vector<CommandRule> TABLE = {
        {"ping", -1, {"stale", "fast"}, 0, 0, 0, "Ping the server"},
        {"echo", 2, {"fast"}, 0, 0, 0, "Echo the given string"},
        {"set", -3, {"write", "denyoom"}, 1, 1, 1, "Set the string value of a key"},
        {"get", 2, {"readonly", "fast"}, 1, 1, 1, "Get the value of a key"},
        {"mset", -3, {"write", "denyoom"}, 1, -1, 2, "Set multiple keys to multiple values"},
        {"mget", -2, {"readonly", "fast"}, 1, -1, 1, "Get the values of all the given keys"},
        {"incr", 2, {"write", "denyoom", "fast"}, 1, 1, 1, "Increment the integer value of a key by one"},
        {"decr", 2, {"write", "denyoom", "fast"}, 1, 1, 1, "Decrement the integer value of a key by one"},
        {"incrby", 3, {"write", "denyoom", "fast"}, 1, 1, 1, "Increment the integer value of a key by the given amount"},
        {"decrby", 3, {"write", "denyoom", "fast"}, 1, 1, 1, "Decrement the integer value of a key by the given amount"},
        {"append", 3, {"write", "denyoom", "fast"}, 1, 1, 1, "Append a value to a key"},
        {"strlen", 2, {"readonly", "fast"}, 1, 1, 1, "Get the length of the value stored in a key"},
        {"lpush", -3, {"write", "denyoom", "fast"}, 1, 1, 1, "Prepend one or multiple elements to a list"},
        {"rpush", -3, {"write", "denyoom", "fast"}, 1, 1, 1, "Append one or multiple elements to a list"},
        {"lpop", -2, {"write", "fast"}, 1, 1, 1, "Remove and get the first elements in a list"},
        {"rpop", -2, {"write", "fast"}, 1, 1, 1, "Remove and get the last elements in a list"},
        {"llen", 2, {"readonly", "fast"}, 1, 1, 1, "Get the length of a list"},
        {"lindex", 3, {"readonly"}, 1, 1, 1, "Get an element from a list by its index"},
        {"lrange", 4, {"readonly"}, 1, 1, 1, "Get a range of elements from a list"},
        {"sadd", -3, {"write", "denyoom", "fast"}, 1, 1, 1, "Add one or more members to a set"},
        {"srem", -3, {"write", "fast"}, 1, 1, 1, "Remove one or more members from a set"},
        {"sismember", 3, {"readonly", "fast"}, 1, 1, 1, "Determine if a given value is a member of a set"},
        {"scard", 2, {"readonly", "fast"}, 1, 1, 1, "Get the number of members in a set"},
        {"smembers", 2, {"readonly"}, 1, 1, 1, "Get all the members in a set"},
        {"srandmember", -2, {"readonly", "random"}, 1, 1, 1, "Get one or multiple random members from a set"},
        {"del", -2, {"write"}, 1, -1, 1, "Delete a key"},
        {"exists", -2, {"readonly", "fast"}, 1, -1, 1, "Determine if a key exists"},
        {"type", 2, {"readonly", "fast"}, 1, 1, 1, "Determine the type stored at key"},
        {"expire", -3, {"write", "fast"}, 1, 1, 1, "Set a key's time to live in seconds"},
        {"pexpire", -3, {"write", "fast"}, 1, 1, 1, "Set a key's time to live in milliseconds"},
        {"expireat", -3, {"write", "fast"}, 1, 1, 1, "Set the expiration for a key as a UNIX timestamp"},
        {"pexpireat", -3, {"write", "fast"}, 1, 1, 1, "Set the expiration for a key as a UNIX timestamp in milliseconds"},
        {"ttl", 2, {"readonly", "fast"}, 1, 1, 1, "Get the time to live for a key in seconds"},
        {"pttl", 2, {"readonly", "fast"}, 1, 1, 1, "Get the time to live for a key in milliseconds"},
        {"persist", 2, {"write", "fast"}, 1, 1, 1, "Remove the expiration from a key"},
        {"keys", 2, {"readonly"}, 0, 0, 0, "Find all keys matching the given pattern"},
        {"flushdb", -1, {"write"}, 0, 0, 0, "Remove all keys from the current database"},
        {"flushall", -1, {"write"}, 0, 0, 0, "Remove all keys from all databases"},
        {"select", 2, {"loading", "stale", "fast"}, 0, 0, 0, "Change the selected database for the current connection"},
        {"dbsize", 1, {"readonly", "fast"}, 0, 0, 0, "Return the number of keys in the selected database"},
        {"time", 1, {"random", "fast", "loading", "stale"}, 0, 0, 0, "Return the current server time"},
        {"info", -1, {"loading", "stale"}, 0, 0, 0, "Get information and statistics about the server"},
        {"config", -2, {"admin", "stale"}, 0, 0, 0, "A container for configuration commands"},
        {"command", -1, {"random", "loading", "stale"}, 0, 0, 0, "Get array of Redis command details"},
        {"client", -2, {"admin", "stale"}, 0, 0, 0, "A container for client connection management commands"},
        {"latency", -2, {"admin", "stale"}, 0, 0, 0, "A container for latency monitoring commands"},
        {"object", -2, {"readonly"}, 2, 2, 1, "Inspect the internals of Redis objects"},
        {"bgrewriteaof", 1, {"admin"}, 0, 0, 0, "Asynchronously rewrite the append-only file"},
        {"save", 1, {"admin", "slow"}, 0, 0, 0, "Synchronously save the dataset to disk"},
        {"bgsave", -1, {"admin"}, 0, 0, 0, "Asynchronously save the dataset to disk in the background"},
        {"lastsave", 1, {"admin", "random", "fast"}, 0, 0, 0, "Get the UNIX timestamp of the last successful save to disk"},
        {"multi", 1, {"fast"}, 0, 0, 0, "Mark the start of a transaction block"},
        {"exec", 1, {"slow"}, 0, 0, 0, "Execute all commands issued after MULTI"},
        {"discard", 1, {"fast"}, 0, 0, 0, "Discard all commands issued after MULTI"}
    };
    return TABLE;
}

static std::string format_command_spec(const CommandRule& rule) {
    std::string out = "*6\r\n";
    out += RespParser::encode_bulk_string(rule.name);
    out += RespParser::encode_integer(rule.arity);
    out += "*" + std::to_string(rule.flags.size()) + "\r\n";
    for (const auto& flag : rule.flags) {
        out += RespParser::encode_simple_string(flag);
    }
    out += RespParser::encode_integer(rule.first_key);
    out += RespParser::encode_integer(rule.last_key);
    out += RespParser::encode_integer(rule.step);
    return out;
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

    if (cmd == "DEL" || cmd == "EXISTS" || cmd == "TYPE" || cmd == "OBJECT" ||
        cmd == "EXPIRE" || cmd == "PEXPIRE" || cmd == "EXPIREAT" || cmd == "PEXPIREAT" ||
        cmd == "TTL" || cmd == "PTTL" || cmd == "PERSIST" || cmd == "KEYS" ||
        cmd == "FLUSHDB" || cmd == "FLUSHALL") {
        return eval_generic(store, db, cmd, tokens);
    }

    if (cmd == "PING" || cmd == "ECHO" || cmd == "SELECT" || cmd == "INFO" ||
        cmd == "CONFIG" || cmd == "COMMAND" || cmd == "TIME" || cmd == "DBSIZE" ||
        cmd == "BGREWRITEAOF" || cmd == "SAVE" || cmd == "BGSAVE" || cmd == "LASTSAVE" ||
        cmd == "CLIENT" || cmd == "LATENCY") {
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
                int64_t sec = 0;
                if (!parse_int(tokens[++i], sec)) return RespParser::encode_error("ERR value is not an integer or out of range");
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                expire_at_ms = static_cast<uint64_t>(now + (sec * 1000));
            } else if (opt == "PX" && i + 1 < tokens.size()) {
                int64_t ms = 0;
                if (!parse_int(tokens[++i], ms)) return RespParser::encode_error("ERR value is not an integer or out of range");
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
                if (obj->encoding() == ObjectEncoding::InlineInt) {
                    out += RespParser::encode_bulk_string(std::to_string(obj->get_int_value()));
                } else {
                    out += RespParser::encode_bulk_string(obj->get_string_view());
                }
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
        if (cmd == "INCRBY" || cmd == "DECRBY") {
            int64_t parsed = 0;
            if (!parse_int(tokens[2], parsed)) {
                return RespParser::encode_error("ERR value is not an integer or out of range");
            }
            delta = (cmd == "DECRBY") ? -parsed : parsed;
        }

        const std::string& key = tokens[1];
        auto obj = db.get(key);
        if (!obj) {
            db.set(key, RunDBObject::create_int(delta));
            return RespParser::encode_integer(delta);
        }

        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        int64_t cur = 0;
        if (obj->encoding() == ObjectEncoding::InlineInt) {
            cur = obj->get_int_value();
        } else {
            if (!parse_int(obj->get_string_view(), cur)) {
                return RespParser::encode_error("ERR value is not an integer or out of range");
            }
        }

        int64_t result = 0;
        if (__builtin_add_overflow(cur, delta, &result)) {
            return RespParser::encode_error("ERR increment or decrement would overflow");
        }
        db.set(key, RunDBObject::create_int(result));
        return RespParser::encode_integer(result);
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

        if (auto* sds = obj->get_sds()) {
            sds->append(suffix);
            obj->update_lru();
            return RespParser::encode_integer(static_cast<int64_t>(sds->size()));
        } else {
            std::string val = obj->get_string_value() + suffix;
            size_t len = val.size();
            db.set(key, RunDBObject::create_string(val));
            return RespParser::encode_integer(static_cast<int64_t>(len));
        }
    }

    if (cmd == "STRLEN") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'strlen' command");
        auto obj = db.get(tokens[1]);
        if (!obj) return RespParser::encode_integer(0);
        if (obj->type() != ObjectType::String) {
            return RespParser::encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        if (obj->encoding() == ObjectEncoding::InlineInt) {
            char buf[32];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), obj->get_int_value());
            return RespParser::encode_integer(static_cast<int64_t>(ptr - buf));
        }
        if (const auto* sds = obj->get_sds()) {
            return RespParser::encode_integer(static_cast<int64_t>(sds->size()));
        }
        return RespParser::encode_integer(static_cast<int64_t>(obj->get_string_view().size()));
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
        int64_t idx = 0;
        if (!parse_int(tokens[2], idx)) return RespParser::encode_error("ERR value is not an integer or out of range");
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
        int64_t start = 0;
        int64_t stop = 0;
        if (!parse_int(tokens[2], start) || !parse_int(tokens[3], stop)) {
            return RespParser::encode_error("ERR value is not an integer or out of range");
        }
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
            members.reserve(hs->size());
            for (const auto& item : *hs) {
                members.push_back(item.to_string());
            }
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

    if (cmd == "OBJECT") {
        if (tokens.size() < 2) return RespParser::encode_error("ERR wrong number of arguments for 'object' command");
        std::string sub = to_upper(tokens[1]);
        if (sub == "HELP") {
            return RespParser::encode_array({
                "OBJECT <subcommand> [<arg> [value] [opt] ...]. Subcommands are:",
                "ENCODING <key>",
                "    Return the internal encoding of the object stored at <key>.",
                "IDLETIME <key>",
                "    Return the idle time in seconds since the last access to <key>.",
                "REFCOUNT <key>",
                "    Return the reference count of the object stored at <key>.",
                "HELP",
                "    Print this help."
            });
        }

        if (sub == "ENCODING") {
            if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'object|encoding' command");
            auto obj = db.get(tokens[2]);
            if (!obj) return RespParser::encode_nil();
            switch (obj->encoding()) {
                case ObjectEncoding::Raw: return RespParser::encode_bulk_string("raw");
                case ObjectEncoding::InlineInt: return RespParser::encode_bulk_string("int");
                case ObjectEncoding::QuickList: return RespParser::encode_bulk_string("quicklist");
                case ObjectEncoding::IntSet: return RespParser::encode_bulk_string("intset");
                case ObjectEncoding::HashSet: return RespParser::encode_bulk_string("hashtable");
            }
            return RespParser::encode_bulk_string("raw");
        }

        if (sub == "IDLETIME") {
            if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'object|idletime' command");
            auto obj = db.get(tokens[2]);
            if (!obj) return RespParser::encode_nil();
            uint32_t now_lru = RunDBObject::get_lru_clock();
            uint32_t obj_lru = obj->lru();
            int64_t idle = 0;
            if (now_lru >= obj_lru) {
                idle = now_lru - obj_lru;
            } else {
                idle = (1 << 24) - obj_lru + now_lru;
            }
            return RespParser::encode_integer(idle);
        }

        if (sub == "REFCOUNT") {
            if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for 'object|refcount' command");
            auto obj = db.get(tokens[2]);
            if (!obj) return RespParser::encode_nil();
            return RespParser::encode_integer(obj.use_count());
        }

        return RespParser::encode_error("ERR Unknown subcommand or wrong number of arguments for 'OBJECT'. Try OBJECT HELP.");
    }

    if (cmd == "EXPIRE" || cmd == "PEXPIRE" || cmd == "EXPIREAT" || cmd == "PEXPIREAT") {
        if (tokens.size() != 3) return RespParser::encode_error("ERR wrong number of arguments for '" + cmd + "' command");
        int64_t val = 0;
        if (!parse_int(tokens[2], val)) return RespParser::encode_error("ERR value is not an integer or out of range");
        uint64_t expire_at_ms = 0;
        if (cmd == "EXPIRE") {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            expire_at_ms = static_cast<uint64_t>(now + (val * 1000));
        } else if (cmd == "PEXPIRE") {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            expire_at_ms = static_cast<uint64_t>(now + val);
        } else if (cmd == "EXPIREAT") {
            expire_at_ms = static_cast<uint64_t>(val * 1000);
        } else if (cmd == "PEXPIREAT") {
            expire_at_ms = static_cast<uint64_t>(val);
        }
        bool ok = db.set_expire(tokens[1], expire_at_ms);
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

    if (cmd == "KEYS") {
        if (tokens.size() != 2) return RespParser::encode_error("ERR wrong number of arguments for 'keys' command");
        std::string_view pattern = tokens[1];
        std::vector<std::string> matched_keys;
        for (const auto& [k, v] : db.dict()) {
            if (db.is_expired(k)) continue;
            if (pattern == "*" || pattern == k) {
                matched_keys.push_back(k);
            }
        }
        return RespParser::encode_array(matched_keys);
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
        int db_id = 0;
        if (!parse_int(tokens[1], db_id) || db_id < 0 || static_cast<size_t>(db_id) >= store.db_count()) {
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
        const auto& table = get_command_table();
        if (tokens.size() == 1) {
            std::string out = "*" + std::to_string(table.size()) + "\r\n";
            for (const auto& rule : table) {
                out += format_command_spec(rule);
            }
            return out;
        }

        std::string sub = to_upper(tokens[1]);
        if (sub == "COUNT") {
            return RespParser::encode_integer(static_cast<int64_t>(table.size()));
        }

        if (sub == "LIST") {
            std::string filter_pat;
            if (tokens.size() >= 4 && to_upper(tokens[2]) == "FILTERBY" && to_upper(tokens[3]) == "PATTERN") {
                filter_pat = tokens.size() >= 5 ? tokens[4] : "*";
            }
            std::vector<std::string> names;
            names.reserve(table.size());
            for (const auto& rule : table) {
                if (filter_pat.empty() || filter_pat == "*" || rule.name.find(filter_pat) != std::string::npos) {
                    names.push_back(rule.name);
                }
            }
            return RespParser::encode_array(names);
        }

        if (sub == "INFO") {
            if (tokens.size() == 2) {
                std::string out = "*" + std::to_string(table.size()) + "\r\n";
                for (const auto& rule : table) {
                    out += format_command_spec(rule);
                }
                return out;
            }
            std::string out = "*" + std::to_string(tokens.size() - 2) + "\r\n";
            for (size_t i = 2; i < tokens.size(); ++i) {
                std::string target = tokens[i];
                for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto it = std::find_if(table.begin(), table.end(), [&](const CommandRule& r) {
                    return r.name == target;
                });
                if (it != table.end()) {
                    out += format_command_spec(*it);
                } else {
                    out += "$-1\r\n";
                }
            }
            return out;
        }

        if (sub == "DOCS") {
            std::vector<const CommandRule*> requested;
            if (tokens.size() == 2) {
                for (const auto& r : table) requested.push_back(&r);
            } else {
                for (size_t i = 2; i < tokens.size(); ++i) {
                    std::string target = tokens[i];
                    for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    auto it = std::find_if(table.begin(), table.end(), [&](const CommandRule& r) {
                        return r.name == target;
                    });
                    if (it != table.end()) requested.push_back(&*it);
                }
            }
            std::string out = "*" + std::to_string(requested.size() * 2) + "\r\n";
            for (const auto* r : requested) {
                out += RespParser::encode_bulk_string(r->name);
                out += "*6\r\n";
                out += RespParser::encode_bulk_string("summary");
                out += RespParser::encode_bulk_string(r->summary);
                out += RespParser::encode_bulk_string("since");
                out += RespParser::encode_bulk_string(rundb::VERSION);
                out += RespParser::encode_bulk_string("group");
                out += RespParser::encode_bulk_string("generic");
            }
            return out;
        }

        if (sub == "GETKEYS") {
            if (tokens.size() < 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'command|getkeys' command");
            }
            std::string target = tokens[2];
            for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto it = std::find_if(table.begin(), table.end(), [&](const CommandRule& r) {
                return r.name == target;
            });
            if (it == table.end()) {
                return RespParser::encode_error("ERR Invalid command specified");
            }
            if (it->first_key == 0) {
                return RespParser::encode_error("ERR The command has no key arguments");
            }

            size_t first = 2 + static_cast<size_t>(it->first_key);
            int last_offset = it->last_key;
            size_t last = (last_offset < 0) ? (tokens.size() + static_cast<size_t>(last_offset)) : (2 + static_cast<size_t>(last_offset));
            size_t step = it->step > 0 ? static_cast<size_t>(it->step) : 1;

            std::vector<std::string> keys;
            for (size_t k = first; k <= last && k < tokens.size(); k += step) {
                keys.push_back(tokens[k]);
            }
            return RespParser::encode_array(keys);
        }

        if (sub == "HELP") {
            return RespParser::encode_array({
                "COMMAND <subcommand> [<arg> [value] ...]. Subcommands are:",
                "COUNT",
                "    Return the total number of commands in this RunDB server.",
                "DOCS [<command-name> ...]",
                "    Return documentation details about commands.",
                "GETKEYS <command> [<arg> ...]",
                "    Extract keys given a full Redis command.",
                "INFO [<command-name> ...]",
                "    Return information about specified commands, or all when no argument is given.",
                "LIST",
                "    Return an array of command names.",
                "HELP",
                "    Print this help."
            });
        }

        return RespParser::encode_error("ERR unknown subcommand or wrong number of arguments for 'command'");
    }

    if (cmd == "INFO") {
        auto mem = Stats::instance().get_memory_stats(store.get_maxmemory());
        std::string info;
        info += "# Server\r\n";
        info += "rundb_version:" + std::string(rundb::VERSION) + "\r\n";
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

        info += "# Persistence\r\n";
        info += "loading:0\r\n";
        auto snap = store.get_snapshot_manager();
        int64_t changes = snap ? snap->changes_since_last_save() : 0;
        int bgsave_in_prog = (snap && snap->bgsave_in_progress()) ? 1 : 0;
        uint64_t last_save = snap ? snap->last_save_time() : 0;
        std::string bgsave_status = snap ? snap->last_bgsave_status() : "ok";
        int64_t bgsave_time = snap ? snap->last_bgsave_time_sec() : -1;

        info += "rdb_changes_since_last_save:" + std::to_string(changes) + "\r\n";
        info += "rdb_bgsave_in_progress:" + std::to_string(bgsave_in_prog) + "\r\n";
        info += "rdb_last_save_time:" + std::to_string(last_save) + "\r\n";
        info += "rdb_last_bgsave_status:" + bgsave_status + "\r\n";
        info += "rdb_last_bgsave_time_sec:" + std::to_string(bgsave_time) + "\r\n";
        info += "rdb_current_bgsave_time_sec:" + std::string(bgsave_in_prog ? "0" : "-1") + "\r\n";

        auto aof = store.get_aof();
        info += "aof_enabled:" + std::string(aof && aof->is_enabled() ? "1" : "0") + "\r\n";
        info += "aof_rewrite_in_progress:0\r\n";
        info += "aof_last_rewrite_time_sec:-1\r\n";
        info += "aof_last_bgrewrite_status:ok\r\n";

        info += "# Keyspace\r\n";
        for (const auto& ks : Stats::instance().get_keyspace_stats(store.databases())) {
            info += "db" + std::to_string(ks.db_id) + ":keys=" + std::to_string(ks.keys) +
                    ",expires=" + std::to_string(ks.expires) +
                    ",avg_ttl=" + std::to_string(ks.avg_ttl_ms) + "\r\n";
        }

        return RespParser::encode_bulk_string(info);
    }

    if (cmd == "BGREWRITEAOF") {
        auto aof = store.get_aof();
        if (!aof) {
            aof = std::make_shared<AOF>("appendonly.aof", AofFsync::EverySec);
            aof->open();
            store.attach_aof(aof);
        }
        if (aof->dump_all(store)) {
            return RespParser::encode_simple_string("Background append only file rewriting started");
        }
        return RespParser::encode_error("ERR Failed to rewrite AOF file");
    }

    if (cmd == "SAVE") {
        auto snap = store.get_snapshot_manager();
        if (!snap) {
            snap = std::make_shared<SnapshotManager>("dump.rdb", 0);
            store.attach_snapshot_manager(snap);
        }
        if (snap->bgsave_in_progress()) {
            return RespParser::encode_error("ERR Background save already in progress");
        }
        if (snap->save(store)) {
            return RespParser::encode_ok();
        }
        return RespParser::encode_error("ERR Failed to save snapshot");
    }

    if (cmd == "BGSAVE") {
        auto snap = store.get_snapshot_manager();
        if (!snap) {
            snap = std::make_shared<SnapshotManager>("dump.rdb", 0);
            store.attach_snapshot_manager(snap);
        }
        if (snap->bgsave_in_progress()) {
            return RespParser::encode_error("ERR Background save already in progress");
        }
        if (snap->bgsave(store)) {
            return RespParser::encode_simple_string("Background saving started");
        }
        return RespParser::encode_error("ERR Failed to start background save");
    }

    if (cmd == "LASTSAVE") {
        auto snap = store.get_snapshot_manager();
        uint64_t t = snap ? snap->last_save_time() : 0;
        return RespParser::encode_integer(static_cast<int64_t>(t));
    }

    if (cmd == "CLIENT") {
        if (tokens.size() < 2) {
            return RespParser::encode_error("ERR wrong number of arguments for 'client' command");
        }
        std::string sub = to_upper(tokens[1]);

        if (sub == "ID") {
            return RespParser::encode_integer(static_cast<int64_t>(ctx.id > 0 ? ctx.id : 1));
        }

        if (sub == "SETNAME") {
            if (tokens.size() != 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'client|setname' command");
            }
            const std::string& name = tokens[2];
            for (char c : name) {
                if (c <= ' ' || c > '~') {
                    return RespParser::encode_error("ERR Client names cannot contain spaces, newlines or special characters.");
                }
            }
            ctx.name = name;
            return RespParser::encode_ok();
        }

        if (sub == "GETNAME") {
            if (ctx.name.empty()) {
                return RespParser::encode_nil();
            }
            return RespParser::encode_bulk_string(ctx.name);
        }

        if (sub == "SETINFO") {
            if (tokens.size() != 4) {
                return RespParser::encode_error("ERR wrong number of arguments for 'client|setinfo' command");
            }
            std::string attr = to_upper(tokens[2]);
            if (attr == "LIB-NAME") {
                ctx.lib_name = tokens[3];
            } else if (attr == "LIB-VER") {
                ctx.lib_ver = tokens[3];
            }
            return RespParser::encode_ok();
        }

        auto format_client_line = [](const ClientContext& c) {
            auto now_s = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            uint64_t age = (now_s >= c.created_time_s) ? (now_s - c.created_time_s) : 0;
            uint64_t idle = (now_s >= c.last_interaction_s) ? (now_s - c.last_interaction_s) : 0;
            std::string line;
            line += "id=" + std::to_string(c.id > 0 ? c.id : 1);
            line += " addr=" + (c.addr.empty() ? "127.0.0.1:0" : c.addr);
            line += " laddr=" + (c.laddr.empty() ? "127.0.0.1:7379" : c.laddr);
            line += " fd=" + std::to_string(c.fd >= 0 ? c.fd : 0);
            line += " name=" + c.name;
            line += " age=" + std::to_string(age);
            line += " idle=" + std::to_string(idle);
            line += " flags=N";
            line += " db=" + std::to_string(c.active_db);
            line += " sub=0 psub=0";
            line += " multi=" + std::to_string(c.in_transaction ? static_cast<int>(c.tx_queue.size()) : -1);
            line += " qbuf=0 qbuf-free=0 argv-mem=0 obl=0 oll=0 omem=0 tot-mem=0 events=r";
            line += " cmd=" + (c.last_cmd.empty() ? "client" : c.last_cmd);
            line += " user=default redir=-1 resp=2\n";
            return line;
        };

        if (sub == "INFO") {
            return RespParser::encode_bulk_string(format_client_line(ctx));
        }

        if (sub == "LIST") {
            const auto& all = store.get_clients();
            std::string list_out;
            if (all.empty()) {
                list_out = format_client_line(ctx);
            } else {
                for (const auto* c : all) {
                    if (c) list_out += format_client_line(*c);
                }
            }
            return RespParser::encode_bulk_string(list_out);
        }

        if (sub == "KILL") {
            if (tokens.size() < 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'client|kill' command");
            }
            std::string target;
            if (tokens.size() >= 4 && to_upper(tokens[2]) == "ID") {
                target = tokens[3];
            } else {
                target = tokens[2];
            }
            bool killed = store.kill_client(target);
            if (to_upper(tokens[2]) == "ID") {
                return RespParser::encode_integer(killed ? 1 : 0);
            }
            return RespParser::encode_ok();
        }

        if (sub == "PAUSE" || sub == "UNPAUSE" || sub == "REPLY" || sub == "NO-TOUCH" || sub == "NO-EVICT") {
            return RespParser::encode_ok();
        }

        if (sub == "HELP") {
            return RespParser::encode_array({
                "CLIENT <subcommand> [<arg> [value] ...]. Subcommands are:",
                "ID",
                "    Return the ID of the current connection.",
                "GETNAME",
                "    Return the name of the current connection.",
                "SETNAME <name>",
                "    Set the name of the current connection.",
                "INFO",
                "    Return information about the current client connection.",
                "LIST [TYPE normal|master|replica|pubsub] [ID <client-id> ...]",
                "    Return information about client connections.",
                "KILL <ip:port> | [ID <client-id>] [TYPE <type>] [SKIPME yes|no]",
                "    Kill connections of clients matching the specified filter.",
                "SETINFO <LIB-NAME|LIB-VER> <value>",
                "    Set client library name or version.",
                "PAUSE <timeout> [WRITE|ALL]",
                "    Suspend clients for timeout ms.",
                "UNPAUSE",
                "    Resume clients suspended by CLIENT PAUSE.",
                "REPLY ON|OFF|SKIP",
                "    Instruct the server whether to reply to commands.",
                "HELP",
                "    Print this help."
            });
        }

        return RespParser::encode_error("ERR unknown subcommand or wrong number of arguments for 'client'");
    }

    if (cmd == "CONFIG") {
        if (tokens.size() < 2) {
            return RespParser::encode_error("ERR wrong number of arguments for 'config' command");
        }
        std::string sub = to_upper(tokens[1]);

        if (sub == "GET") {
            if (tokens.size() < 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'config|get' command");
            }
            std::string pat = tokens[2];
            for (char& c : pat) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            auto match_pattern = [](std::string_view p, std::string_view name) {
                if (p == "*" || p == name) return true;
                if (!p.empty() && p.back() == '*') {
                    std::string_view prefix = p.substr(0, p.size() - 1);
                    return name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix;
                }
                return false;
            };

            const auto& cfg = store.get_config();
            std::vector<std::pair<std::string, std::string>> all_params = {
                {"maxmemory", std::to_string(store.get_maxmemory())},
                {"maxmemory-policy", Eviction::policy_to_string(store.get_eviction_policy())},
                {"appendonly", (store.get_aof() && store.get_aof()->is_enabled()) ? "yes" : "no"},
                {"appendfsync", cfg.aof_fsync.empty() ? "everysec" : cfg.aof_fsync},
                {"port", std::to_string(cfg.port > 0 ? cfg.port : 7379)},
                {"bind", cfg.host.empty() ? "127.0.0.1" : cfg.host},
                {"loglevel", cfg.log_level.empty() ? "notice" : cfg.log_level},
                {"databases", std::to_string(store.db_count())},
                {"maxclients", std::to_string(cfg.max_clients > 0 ? cfg.max_clients : 10000)},
                {"dir", "."},
                {"dbfilename", cfg.snapshot_file.empty() ? "dump.rdb" : cfg.snapshot_file},
                {"snapshot-file", cfg.snapshot_file.empty() ? "dump.rdb" : cfg.snapshot_file},
                {"snapshot-interval", cfg.snapshot_interval_str},
                {"save", cfg.snapshot_interval_str},
                {"appendfilename", cfg.aof_file.empty() ? "appendonly.aof" : cfg.aof_file},
                {"timeout", "0"},
                {"hz", "10"},
                {"tcp-backlog", "511"},
                {"daemonize", "no"}
            };

            std::vector<std::string> resp;
            for (const auto& [k, v] : all_params) {
                if (match_pattern(pat, k)) {
                    resp.push_back(k);
                    resp.push_back(v);
                }
            }
            return RespParser::encode_array(resp);
        }

        if (sub == "SET") {
            if (tokens.size() < 4 || (tokens.size() % 2) != 0) {
                return RespParser::encode_error("ERR wrong number of arguments for 'config|set' command");
            }
            auto s_ends_with = [](std::string_view s, std::string_view suff) {
                return s.size() >= suff.size() && s.substr(s.size() - suff.size()) == suff;
            };

            for (size_t i = 2; i < tokens.size(); i += 2) {
                std::string param = tokens[i];
                for (char& c : param) {
                    if (c == '_') c = '-';
                    else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                const std::string& val = tokens[i + 1];

                if (param == "maxmemory" || param == "memory-limit") {
                    size_t bytes = 0;
                    try {
                        std::string lower = val;
                        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        size_t mult = 1;
                        if (s_ends_with(lower, "gb") || s_ends_with(lower, "g")) {
                            mult = 1024ULL * 1024ULL * 1024ULL;
                            lower.erase(lower.find_last_not_of("gb") + 1);
                        } else if (s_ends_with(lower, "mb") || s_ends_with(lower, "m")) {
                            mult = 1024ULL * 1024ULL;
                            lower.erase(lower.find_last_not_of("mb") + 1);
                        } else if (s_ends_with(lower, "kb") || s_ends_with(lower, "k")) {
                            mult = 1024ULL;
                            lower.erase(lower.find_last_not_of("kb") + 1);
                        }
                        bytes = static_cast<size_t>(std::stoull(lower) * mult);
                    } catch (...) {
                        return RespParser::encode_error("ERR Invalid argument '" + val + "' for CONFIG SET 'maxmemory'");
                    }
                    store.set_maxmemory(bytes);
                } else if (param == "maxmemory-policy" || param == "eviction-strategy") {
                    try {
                        auto pol = Eviction::parse_policy(val);
                        store.set_eviction_policy(pol);
                    } catch (...) {
                        return RespParser::encode_error("ERR Invalid argument '" + val + "' for CONFIG SET 'maxmemory-policy'");
                    }
                } else if (param == "appendonly" || param == "aof-enabled") {
                    bool enable = (val == "yes" || val == "true" || val == "1");
                    if (enable) {
                        if (!store.get_aof()) {
                            auto aof = std::make_shared<AOF>(store.get_config().aof_file.empty() ? "appendonly.aof" : store.get_config().aof_file, AofFsync::EverySec);
                            aof->open();
                            store.attach_aof(aof);
                        }
                    } else {
                        store.detach_aof();
                    }
                } else if (param == "appendfsync" || param == "aof-fsync") {
                    try {
                        auto fs = AOF::parse_fsync(val);
                        if (auto aof = store.get_aof()) {
                            aof->set_fsync_policy(fs);
                        }
                        store.get_config().aof_fsync = val;
                    } catch (...) {
                        return RespParser::encode_error("ERR Invalid argument '" + val + "' for CONFIG SET 'appendfsync'");
                    }
                } else if (param == "snapshot-interval" || param == "save") {
                    try {
                        uint64_t sec = SnapshotManager::parse_interval(val);
                        store.get_config().snapshot_interval_str = val;
                        store.get_config().snapshot_interval_sec = sec;
                        auto snap = store.get_snapshot_manager();
                        if (!snap) {
                            snap = std::make_shared<SnapshotManager>(store.get_config().snapshot_file, sec);
                            store.attach_snapshot_manager(snap);
                        } else {
                            snap->set_interval_sec(sec);
                        }
                    } catch (const std::exception& e) {
                        return RespParser::encode_error(std::string("ERR ") + e.what());
                    }
                } else if (param == "snapshot-file" || param == "dbfilename") {
                    store.get_config().snapshot_file = val;
                    auto snap = store.get_snapshot_manager();
                    if (snap) {
                        snap->set_filepath(val);
                    }
                } else if (param == "loglevel" || param == "log-level") {
                    store.get_config().log_level = val;
                } else if (param == "maxclients" || param == "max-clients") {
                    try {
                        store.get_config().max_clients = static_cast<size_t>(std::stoull(val));
                    } catch (...) {
                        return RespParser::encode_error("ERR Invalid argument '" + val + "' for CONFIG SET 'maxclients'");
                    }
                } else if (param == "dir" || param == "timeout") {
                    // Accepted benign configs
                } else {
                    return RespParser::encode_error("ERR Unsupported CONFIG parameter: " + param);
                }
            }
            return RespParser::encode_ok();
        }

        if (sub == "RESETSTAT") {
            Stats::instance().reset_stats();
            return RespParser::encode_ok();
        }

        if (sub == "REWRITE") {
            return RespParser::encode_ok();
        }

        if (sub == "HELP") {
            return RespParser::encode_array({
                "CONFIG <subcommand> [<arg> [value] ...]. Subcommands are:",
                "GET <pattern>",
                "    Return configuration parameters matching pattern.",
                "SET <directive> <value>",
                "    Set configuration parameter to value.",
                "RESETSTAT",
                "    Reset server statistics.",
                "REWRITE",
                "    Rewrite configuration file on disk.",
                "HELP",
                "    Print this help."
            });
        }

        return RespParser::encode_error("ERR unknown subcommand or wrong number of arguments for 'config'");
    }

    if (cmd == "LATENCY") {
        if (tokens.size() < 2) {
            return RespParser::encode_error("ERR wrong number of arguments for 'latency' command");
        }
        std::string sub = to_upper(tokens[1]);

        if (sub == "LATEST") {
            auto latest = LatencyMonitor::instance().get_latest();
            if (latest.empty()) {
                return "*0\r\n";
            }
            std::string out = "*" + std::to_string(latest.size()) + "\r\n";
            for (const auto& item : latest) {
                out += "*4\r\n";
                out += RespParser::encode_bulk_string(item.event);
                out += RespParser::encode_integer(static_cast<int64_t>(item.timestamp_s));
                out += RespParser::encode_integer(static_cast<int64_t>(item.latest_ms));
                out += RespParser::encode_integer(static_cast<int64_t>(item.max_ms));
            }
            return out;
        }

        if (sub == "HISTORY") {
            if (tokens.size() != 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'latency|history' command");
            }
            auto history = LatencyMonitor::instance().get_history(tokens[2]);
            if (history.empty()) {
                return "*0\r\n";
            }
            std::string out = "*" + std::to_string(history.size()) + "\r\n";
            for (const auto& s : history) {
                out += "*2\r\n";
                out += RespParser::encode_integer(static_cast<int64_t>(s.timestamp_s));
                out += RespParser::encode_integer(static_cast<int64_t>(s.latency_ms));
            }
            return out;
        }

        if (sub == "RESET") {
            std::vector<std::string> events;
            for (size_t i = 2; i < tokens.size(); ++i) {
                events.push_back(tokens[i]);
            }
            size_t count = LatencyMonitor::instance().reset(events);
            return RespParser::encode_integer(static_cast<int64_t>(count));
        }

        if (sub == "GRAPH") {
            if (tokens.size() != 3) {
                return RespParser::encode_error("ERR wrong number of arguments for 'latency|graph' command");
            }
            std::string graph = LatencyMonitor::instance().generate_graph(tokens[2]);
            return RespParser::encode_bulk_string(graph);
        }

        if (sub == "DOCTOR") {
            std::string doc = LatencyMonitor::instance().generate_doctor();
            return RespParser::encode_bulk_string(doc);
        }

        if (sub == "HELP") {
            return RespParser::encode_array(LatencyMonitor::get_help());
        }

        return RespParser::encode_error("ERR unknown subcommand or wrong number of arguments for 'latency'");
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
            // Log to AOF if write command
            if (auto aof = store.get_aof(); aof && aof->is_enabled() && !sub_tokens.empty()) {
                std::string sub_cmd = sub_tokens[0];
                for (char& c : sub_cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                static const std::unordered_set<std::string> WRITE_CMDS = {
                    "SET", "MSET", "INCR", "DECR", "INCRBY", "DECRBY", "APPEND",
                    "LPUSH", "RPUSH", "LPOP", "RPOP",
                    "SADD", "SREM",
                    "DEL", "FLUSHDB", "FLUSHALL", "EXPIRE", "PEXPIRE", "EXPIREAT", "PEXPIREAT", "PERSIST",
                    "SELECT"
                };
                if (WRITE_CMDS.find(sub_cmd) != WRITE_CMDS.end()) {
                    if (aof && aof->is_enabled()) {
                        aof->log_command(sub_tokens);
                    }
                    if (auto snap = store.get_snapshot_manager()) {
                        snap->notify_keyspace_changed();
                    }
                }
            }
        }
        return reply;
    }

    return RespParser::encode_error("ERR unknown transaction command");
}

} // namespace rundb::core
