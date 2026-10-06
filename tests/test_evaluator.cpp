#include "test_framework.hpp"
#include "core/store.hpp"
#include "core/evaluator.hpp"
#include "core/client_context.hpp"
#include "core/latency.hpp"
#include <thread>
#include <chrono>

using namespace rundb::core;

TEST_CASE("Evaluator_PingAndEcho") {
    Store store;
    ClientContext ctx;

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"PING"}), "+PONG\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"PING", "custom_msg"}), "$10\r\ncustom_msg\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"ECHO", "HELLO"}), "$5\r\nHELLO\r\n");
}

TEST_CASE("Evaluator_StringOperations") {
    Store store;
    ClientContext ctx;

    // SET and GET
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "mykey", "myval"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "mykey"}), "$5\r\nmyval\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "nonexistent"}), "$-1\r\n");

    // APPEND and STRLEN
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"APPEND", "mykey", "_suffix"}), ":12\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "mykey"}), "$12\r\nmyval_suffix\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"STRLEN", "mykey"}), ":12\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"STRLEN", "nonexistent"}), ":0\r\n");

    // MSET and MGET
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"MSET", "k1", "v1", "k2", "v2"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"MGET", "k1", "k2", "missing"}), "*3\r\n$2\r\nv1\r\n$2\r\nv2\r\n$-1\r\n");

    // INCR, DECR, INCRBY, DECRBY
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "counter", "10"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"INCR", "counter"}), ":11\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"INCRBY", "counter", "5"}), ":16\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"DECR", "counter"}), ":15\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"DECRBY", "counter", "10"}), ":5\r\n");

    // INCR on non-existent key sets it to 1
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"INCR", "fresh_counter"}), ":1\r\n");

    // SET NX and XX options
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "nx_key", "val1", "NX"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "nx_key", "val2", "NX"}), "$-1\r\n"); // NX fails because key exists
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "missing_xx", "val", "XX"}), "$-1\r\n"); // XX fails because key does not exist
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "nx_key", "val3", "XX"}), "+OK\r\n");
}

TEST_CASE("Evaluator_ListOperations") {
    Store store;
    ClientContext ctx;

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"RPUSH", "mylist", "a", "b", "c"}), ":3\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LPUSH", "mylist", "front"}), ":4\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LLEN", "mylist"}), ":4\r\n");

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LINDEX", "mylist", "0"}), "$5\r\nfront\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LINDEX", "mylist", "-1"}), "$1\r\nc\r\n");

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LRANGE", "mylist", "0", "-1"}),
              "*4\r\n$5\r\nfront\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LPOP", "mylist"}), "$5\r\nfront\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"RPOP", "mylist"}), "$1\r\nc\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LLEN", "mylist"}), ":2\r\n");
}

TEST_CASE("Evaluator_SetOperations") {
    Store store;
    ClientContext ctx;

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SADD", "myset", "10", "20", "30"}), ":3\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SCARD", "myset"}), ":3\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SISMEMBER", "myset", "20"}), ":1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SISMEMBER", "myset", "99"}), ":0\r\n");

    // Add string to promote IntSet to HashSet
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SADD", "myset", "str_val"}), ":1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SISMEMBER", "myset", "str_val"}), ":1\r\n");

    // SREM
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SREM", "myset", "10"}), ":1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SCARD", "myset"}), ":3\r\n");
}

TEST_CASE("Evaluator_GenericCommands") {
    Store store;
    ClientContext ctx;

    store.process_command(ctx, {"SET", "k_str", "val"});
    store.process_command(ctx, {"RPUSH", "k_list", "item"});
    store.process_command(ctx, {"SADD", "k_set", "123"});

    // TYPE
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"TYPE", "k_str"}), "+string\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"TYPE", "k_list"}), "+list\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"TYPE", "k_set"}), "+set\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"TYPE", "k_missing"}), "+none\r\n");

    // EXISTS and DEL
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"EXISTS", "k_str", "k_list", "k_missing"}), ":2\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"DEL", "k_str", "k_list"}), ":2\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"EXISTS", "k_str"}), ":0\r\n");

    // EXPIRE, TTL, PERSIST
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "exp_key", "val", "EX", "100"}), "+OK\r\n");
    std::string ttl_res = Evaluator::evaluate(store, ctx, {"TTL", "exp_key"});
    ASSERT_CONTAINS(ttl_res, ":");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"PERSIST", "exp_key"}), ":1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"TTL", "exp_key"}), ":-1\r\n");

    // FLUSHDB
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"FLUSHDB"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"DBSIZE"}), ":0\r\n");
}

TEST_CASE("Evaluator_MultiDatabaseIsolation") {
    Store store;
    ClientContext ctx;

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SELECT", "0"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "common", "val_db0"}), "+OK\r\n");

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SELECT", "1"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "common", "val_db1"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "common"}), "$7\r\nval_db1\r\n");

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SELECT", "0"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "common"}), "$7\r\nval_db0\r\n");

    // Out of range DB index
    std::string err_sel = Evaluator::evaluate(store, ctx, {"SELECT", "99"});
    ASSERT_CONTAINS(err_sel, "ERR");
}

TEST_CASE("Evaluator_Transactions") {
    Store store;
    ClientContext ctx;

    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"MULTI"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "tx_k", "tx_v"}), "+QUEUED\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"INCR", "tx_cnt"}), "+QUEUED\r\n");

    // EXEC executes queued commands
    std::string exec_res = Evaluator::evaluate(store, ctx, {"EXEC"});
    ASSERT_EQ(exec_res, "*2\r\n+OK\r\n:1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "tx_k"}), "$4\r\ntx_v\r\n");

    // DISCARD cancels transaction
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"MULTI"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"SET", "disc_k", "val"}), "+QUEUED\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"DISCARD"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"GET", "disc_k"}), "$-1\r\n");

    // EXEC without MULTI error
    std::string err_exec = Evaluator::evaluate(store, ctx, {"EXEC"});
    ASSERT_CONTAINS(err_exec, "ERR");
}

TEST_CASE("Evaluator_WrongTypeSafety") {
    Store store;
    ClientContext ctx;

    Evaluator::evaluate(store, ctx, {"SET", "str_key", "string_val"});
    std::string wt1 = Evaluator::evaluate(store, ctx, {"LPUSH", "str_key", "elem"});
    ASSERT_CONTAINS(wt1, "WRONGTYPE");
    std::string wt2 = Evaluator::evaluate(store, ctx, {"SADD", "str_key", "member"});
    ASSERT_CONTAINS(wt2, "WRONGTYPE");
    std::string wt3 = Evaluator::evaluate(store, ctx, {"LPOP", "str_key"});
    ASSERT_CONTAINS(wt3, "WRONGTYPE");
}

TEST_CASE("Evaluator_InvalidIntegerHandling") {
    Store store;
    ClientContext ctx;

    // SET EX / PX with invalid numbers
    std::string res1 = Evaluator::evaluate(store, ctx, {"SET", "k", "v", "EX", "abc"});
    ASSERT_CONTAINS(res1, "ERR");
    std::string res2 = Evaluator::evaluate(store, ctx, {"SET", "k", "v", "PX", "not_a_num"});
    ASSERT_CONTAINS(res2, "ERR");

    // INCRBY / DECRBY with invalid numbers
    std::string res3 = Evaluator::evaluate(store, ctx, {"INCRBY", "k", "xyz"});
    ASSERT_CONTAINS(res3, "ERR");
    std::string res4 = Evaluator::evaluate(store, ctx, {"DECRBY", "k", "xyz"});
    ASSERT_CONTAINS(res4, "ERR");

    // LINDEX / LRANGE with invalid numbers
    Evaluator::evaluate(store, ctx, {"RPUSH", "my_list", "a", "b"});
    std::string res5 = Evaluator::evaluate(store, ctx, {"LINDEX", "my_list", "invalid"});
    ASSERT_CONTAINS(res5, "ERR");
    std::string res6 = Evaluator::evaluate(store, ctx, {"LRANGE", "my_list", "foo", "bar"});
    ASSERT_CONTAINS(res6, "ERR");

    // EXPIRE with invalid number
    std::string res7 = Evaluator::evaluate(store, ctx, {"EXPIRE", "my_list", "bad"});
    ASSERT_CONTAINS(res7, "ERR");

    // SELECT with invalid number
    std::string res8 = Evaluator::evaluate(store, ctx, {"SELECT", "bad"});
    ASSERT_CONTAINS(res8, "ERR");
}

TEST_CASE("Evaluator_InlineIntMgetStrlen") {
    Store store;
    ClientContext ctx;

    // Numbers stored as InlineInt
    Evaluator::evaluate(store, ctx, {"SET", "num_key", "12345"});
    Evaluator::evaluate(store, ctx, {"SET", "str_key", "hello"});

    // STRLEN on InlineInt
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"STRLEN", "num_key"}), ":5\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"STRLEN", "str_key"}), ":5\r\n");

    // MGET combining InlineInt, regular string, and missing key
    std::string mget_res = Evaluator::evaluate(store, ctx, {"MGET", "num_key", "str_key", "missing_key"});
    ASSERT_EQ(mget_res, "*3\r\n$5\r\n12345\r\n$5\r\nhello\r\n$-1\r\n");
}

TEST_CASE("Evaluator_IncrNonNumericString") {
    Store store;
    ClientContext ctx;

    Evaluator::evaluate(store, ctx, {"SET", "str_val", "not_a_number"});
    std::string res = Evaluator::evaluate(store, ctx, {"INCR", "str_val"});
    ASSERT_CONTAINS(res, "ERR");
}

TEST_CASE("Evaluator_KeysCommand") {
    Store store;
    ClientContext ctx;

    Evaluator::evaluate(store, ctx, {"SET", "key_a", "1"});
    Evaluator::evaluate(store, ctx, {"SET", "key_b", "2"});

    std::string all_keys = Evaluator::evaluate(store, ctx, {"KEYS", "*"});
    ASSERT_CONTAINS(all_keys, "key_a");
    ASSERT_CONTAINS(all_keys, "key_b");

    std::string single_key = Evaluator::evaluate(store, ctx, {"KEYS", "key_a"});
    ASSERT_CONTAINS(single_key, "key_a");
    ASSERT_FALSE(single_key.find("key_b") != std::string::npos);
}

TEST_CASE("Evaluator_ExpireAtAndPExpireAt") {
    Store store;
    ClientContext ctx;

    Evaluator::evaluate(store, ctx, {"SET", "expire_key", "val"});
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string res1 = Evaluator::evaluate(store, ctx, {"EXPIREAT", "expire_key", std::to_string(now_sec + 100)});
    ASSERT_EQ(res1, ":1\r\n");

    std::string ttl_res = Evaluator::evaluate(store, ctx, {"TTL", "expire_key"});
    ASSERT_CONTAINS(ttl_res, ":");
    ASSERT_FALSE(ttl_res == ":-1\r\n");
    ASSERT_FALSE(ttl_res == ":-2\r\n");
}

TEST_CASE("Evaluator_CommandCommands") {
    Store store;
    ClientContext ctx;

    // 1. COMMAND returns all command specs
    std::string full_specs = Evaluator::evaluate(store, ctx, {"COMMAND"});
    ASSERT_CONTAINS(full_specs, "ping");
    ASSERT_CONTAINS(full_specs, "set");
    ASSERT_CONTAINS(full_specs, "get");

    // 2. COMMAND COUNT returns total count of commands
    std::string count_res = Evaluator::evaluate(store, ctx, {"COMMAND", "COUNT"});
    ASSERT_CONTAINS(count_res, ":54\r\n");

    // 3. COMMAND LIST returns command names
    std::string list_res = Evaluator::evaluate(store, ctx, {"COMMAND", "LIST"});
    ASSERT_CONTAINS(list_res, "ping");
    ASSERT_CONTAINS(list_res, "echo");
    ASSERT_CONTAINS(list_res, "config");
    ASSERT_CONTAINS(list_res, "latency");

    // 4. COMMAND INFO
    std::string info_res = Evaluator::evaluate(store, ctx, {"COMMAND", "INFO", "SET", "GET"});
    ASSERT_CONTAINS(info_res, "set");
    ASSERT_CONTAINS(info_res, "get");

    // 5. COMMAND DOCS
    std::string docs_res = Evaluator::evaluate(store, ctx, {"COMMAND", "DOCS", "PING"});
    ASSERT_CONTAINS(docs_res, "ping");
    ASSERT_CONTAINS(docs_res, "summary");

    // 6. COMMAND GETKEYS
    std::string keys_res1 = Evaluator::evaluate(store, ctx, {"COMMAND", "GETKEYS", "SET", "foo", "bar"});
    ASSERT_EQ(keys_res1, "*1\r\n$3\r\nfoo\r\n");

    std::string keys_res2 = Evaluator::evaluate(store, ctx, {"COMMAND", "GETKEYS", "MSET", "k1", "v1", "k2", "v2"});
    ASSERT_EQ(keys_res2, "*2\r\n$2\r\nk1\r\n$2\r\nk2\r\n");

    // 7. COMMAND HELP
    std::string help_res = Evaluator::evaluate(store, ctx, {"COMMAND", "HELP"});
    ASSERT_CONTAINS(help_res, "COMMAND");
    ASSERT_CONTAINS(help_res, "COUNT");
}

TEST_CASE("Evaluator_ConfigCommands") {
    Store store(1048576, EvictionPolicy::AllKeysLRU);
    ClientContext ctx;

    // 1. CONFIG GET
    std::string get_all = Evaluator::evaluate(store, ctx, {"CONFIG", "GET", "*"});
    ASSERT_CONTAINS(get_all, "maxmemory");
    ASSERT_CONTAINS(get_all, "maxmemory-policy");
    ASSERT_CONTAINS(get_all, "1048576");
    ASSERT_CONTAINS(get_all, "allkeys-lru");

    std::string get_single = Evaluator::evaluate(store, ctx, {"CONFIG", "GET", "maxmemory"});
    ASSERT_CONTAINS(get_single, "maxmemory");
    ASSERT_CONTAINS(get_single, "1048576");

    // 2. CONFIG SET
    std::string set_mem = Evaluator::evaluate(store, ctx, {"CONFIG", "SET", "maxmemory", "50mb"});
    ASSERT_EQ(set_mem, "+OK\r\n");
    ASSERT_EQ(store.get_maxmemory(), 50ULL * 1024ULL * 1024ULL);

    std::string set_pol = Evaluator::evaluate(store, ctx, {"CONFIG", "SET", "maxmemory-policy", "noeviction"});
    ASSERT_EQ(set_pol, "+OK\r\n");
    ASSERT_EQ(static_cast<int>(store.get_eviction_policy()), static_cast<int>(EvictionPolicy::NoEviction));

    // 3. CONFIG RESETSTAT
    std::string reset_res = Evaluator::evaluate(store, ctx, {"CONFIG", "RESETSTAT"});
    ASSERT_EQ(reset_res, "+OK\r\n");

    // 4. CONFIG REWRITE
    std::string rewrite_res = Evaluator::evaluate(store, ctx, {"CONFIG", "REWRITE"});
    ASSERT_EQ(rewrite_res, "+OK\r\n");

    // 5. CONFIG HELP
    std::string help_res = Evaluator::evaluate(store, ctx, {"CONFIG", "HELP"});
    ASSERT_CONTAINS(help_res, "CONFIG");
    ASSERT_CONTAINS(help_res, "RESETSTAT");
}

TEST_CASE("Evaluator_ClientCommands") {
    Store store;
    ClientContext ctx;
    ctx.id = 42;
    store.register_client(&ctx);

    // 1. CLIENT ID
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "ID"}), ":42\r\n");

    // 2. CLIENT SETNAME and GETNAME
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "GETNAME"}), "$-1\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "SETNAME", "my_client"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "GETNAME"}), "$9\r\nmy_client\r\n");

    // Invalid client name with spaces
    ASSERT_CONTAINS(Evaluator::evaluate(store, ctx, {"CLIENT", "SETNAME", "invalid name"}), "ERR");

    // 3. CLIENT SETINFO
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "SETINFO", "LIB-NAME", "redis-py"}), "+OK\r\n");
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "SETINFO", "LIB-VER", "5.0.0"}), "+OK\r\n");

    // 4. CLIENT INFO
    std::string info_res = Evaluator::evaluate(store, ctx, {"CLIENT", "INFO"});
    ASSERT_CONTAINS(info_res, "id=42");
    ASSERT_CONTAINS(info_res, "name=my_client");

    // 5. CLIENT LIST
    std::string list_res = Evaluator::evaluate(store, ctx, {"CLIENT", "LIST"});
    ASSERT_CONTAINS(list_res, "id=42");

    // 6. CLIENT KILL
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"CLIENT", "KILL", "ID", "42"}), ":1\r\n");
    ASSERT_TRUE(ctx.close_requested);

    // 7. CLIENT HELP
    std::string help_res = Evaluator::evaluate(store, ctx, {"CLIENT", "HELP"});
    ASSERT_CONTAINS(help_res, "CLIENT");
    ASSERT_CONTAINS(help_res, "SETNAME");

    store.unregister_client(&ctx);
}

TEST_CASE("Evaluator_LatencyCommands") {
    Store store;
    ClientContext ctx;

    // Reset before test
    Evaluator::evaluate(store, ctx, {"LATENCY", "RESET"});

    // 1. LATENCY DOCTOR with no events
    ASSERT_CONTAINS(Evaluator::evaluate(store, ctx, {"LATENCY", "DOCTOR"}), "No latency spikes were detected!");

    // 2. LATENCY LATEST with no events
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"LATENCY", "LATEST"}), "*0\r\n");

    // Record mock latency event
    LatencyMonitor::instance().record_event("command", 15);
    LatencyMonitor::instance().record_event("command", 30);

    // 3. LATENCY LATEST with events
    std::string latest_res = Evaluator::evaluate(store, ctx, {"LATENCY", "LATEST"});
    ASSERT_CONTAINS(latest_res, "command");

    // 4. LATENCY HISTORY
    std::string history_res = Evaluator::evaluate(store, ctx, {"LATENCY", "HISTORY", "command"});
    ASSERT_CONTAINS(history_res, ":30\r\n");

    // 5. LATENCY GRAPH
    std::string graph_res = Evaluator::evaluate(store, ctx, {"LATENCY", "GRAPH", "command"});
    ASSERT_CONTAINS(graph_res, "command");

    // 6. LATENCY DOCTOR with events
    std::string doc_res = Evaluator::evaluate(store, ctx, {"LATENCY", "DOCTOR"});
    ASSERT_CONTAINS(doc_res, "The following latency events were analyzed:");
    ASSERT_CONTAINS(doc_res, "command");

    // 7. LATENCY RESET
    std::string reset_res = Evaluator::evaluate(store, ctx, {"LATENCY", "RESET", "command"});
    ASSERT_CONTAINS(reset_res, ":1\r\n");

    // 8. LATENCY HELP
    std::string help_res = Evaluator::evaluate(store, ctx, {"LATENCY", "HELP"});
    ASSERT_CONTAINS(help_res, "LATENCY");
    ASSERT_CONTAINS(help_res, "DOCTOR");
}

TEST_CASE("Evaluator_ObjectCommands") {
    Store store;
    ClientContext ctx;

    // 1. Non-existent key
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "nokey"}), "$-1\r\n");

    // 2. Integer string -> int encoding
    Evaluator::evaluate(store, ctx, {"SET", "num", "42"});
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "num"}), "$3\r\nint\r\n");

    // 3. Regular string -> raw (SDS) encoding
    Evaluator::evaluate(store, ctx, {"SET", "str", "hello_world"});
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "str"}), "$3\r\nraw\r\n");

    // 4. List -> quicklist encoding
    Evaluator::evaluate(store, ctx, {"LPUSH", "mylist", "a", "b", "c"});
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "mylist"}), "$9\r\nquicklist\r\n");

    // 5. Integer Set -> intset encoding
    Evaluator::evaluate(store, ctx, {"SADD", "myset", "100", "200", "300"});
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "myset"}), "$6\r\nintset\r\n");

    // 6. Non-integer element added -> auto-promotes to hashtable encoding
    Evaluator::evaluate(store, ctx, {"SADD", "myset", "string_member"});
    ASSERT_EQ(Evaluator::evaluate(store, ctx, {"OBJECT", "ENCODING", "myset"}), "$9\r\nhashtable\r\n");

    // 7. OBJECT IDLETIME & REFCOUNT
    std::string idletime_res = Evaluator::evaluate(store, ctx, {"OBJECT", "IDLETIME", "num"});
    ASSERT_TRUE(idletime_res.rfind(":", 0) == 0); // integer response :<num>\r\n

    std::string refcount_res = Evaluator::evaluate(store, ctx, {"OBJECT", "REFCOUNT", "num"});
    ASSERT_TRUE(refcount_res.rfind(":", 0) == 0);

    // 8. OBJECT HELP
    std::string help_res = Evaluator::evaluate(store, ctx, {"OBJECT", "HELP"});
    ASSERT_CONTAINS(help_res, "ENCODING");
    ASSERT_CONTAINS(help_res, "IDLETIME");
}


