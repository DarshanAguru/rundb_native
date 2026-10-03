#include "test_framework.hpp"
#include "core/aof.hpp"
#include "core/store.hpp"
#include <cstdio>
#include <filesystem>

using namespace rundb::core;

TEST_CASE("AOF_ParseFsyncPolicies") {
    ASSERT_EQ(static_cast<int>(AOF::parse_fsync("always")), static_cast<int>(AofFsync::Always));
    ASSERT_EQ(static_cast<int>(AOF::parse_fsync("everysec")), static_cast<int>(AofFsync::EverySec));
    ASSERT_EQ(static_cast<int>(AOF::parse_fsync("no")), static_cast<int>(AofFsync::No));
    // Default fallback
    ASSERT_EQ(static_cast<int>(AOF::parse_fsync("invalid")), static_cast<int>(AofFsync::EverySec));
}

TEST_CASE("AOF_LogAndReplayRecovery") {
    const std::string aof_file = "test_persistence.aof";
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }

    // 1. Create AOF and log operations
    {
        AOF aof(aof_file, AofFsync::Always);
        ASSERT_TRUE(aof.open());

        aof.log_command({"SELECT", "0"});
        aof.log_command({"SET", "replayed_str", "hello_aof"});
        aof.log_command({"RPUSH", "replayed_list", "item1", "item2"});
        aof.log_command({"SADD", "replayed_set", "10", "20", "30"});
        aof.log_command({"SELECT", "1"});
        aof.log_command({"SET", "db1_key", "db1_val"});

        aof.close();
    }

    ASSERT_TRUE(std::filesystem::exists(aof_file));
    ASSERT_GT(std::filesystem::file_size(aof_file), 0);

    // 2. Replay into a fresh Store
    {
        Store fresh_store;
        AOF replayer(aof_file, AofFsync::No);
        ASSERT_TRUE(replayer.load_into(fresh_store));

        // Check DB 0
        Database& db0 = fresh_store.get_db(0);
        auto str_obj = db0.get("replayed_str");
        ASSERT_TRUE(str_obj != nullptr);
        ASSERT_EQ(str_obj->get_string_value(), "hello_aof");

        auto list_obj = db0.get("replayed_list");
        ASSERT_TRUE(list_obj != nullptr);
        ASSERT_EQ(list_obj->get_list()->size(), 2);
        ASSERT_EQ(list_obj->get_list()->at(0).value_or(""), "item1");

        auto set_obj = db0.get("replayed_set");
        ASSERT_TRUE(set_obj != nullptr);
        ASSERT_TRUE(set_obj->set_contains("20"));

        // Check DB 1
        Database& db1 = fresh_store.get_db(1);
        auto db1_obj = db1.get("db1_key");
        ASSERT_TRUE(db1_obj != nullptr);
        ASSERT_EQ(db1_obj->get_string_value(), "db1_val");
    }

    // Cleanup
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }
}

TEST_CASE("AOF_DumpAllCompaction") {
    const std::string aof_file = "test_dump_all.aof";
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }

    Store store;
    ClientContext ctx;
    store.process_command(ctx, {"SET", "compact_str", "compacted"});
    store.process_command(ctx, {"RPUSH", "compact_list", "val1", "val2"});
    store.process_command(ctx, {"SADD", "compact_set", "100", "200"});

    auto aof = std::make_shared<AOF>(aof_file, AofFsync::Always);
    ASSERT_TRUE(aof->dump_all(store));

    ASSERT_TRUE(std::filesystem::exists(aof_file));
    ASSERT_GT(std::filesystem::file_size(aof_file), 0);

    // Verify recovery from dumped state
    Store reloaded_store;
    ASSERT_TRUE(aof->load_into(reloaded_store));

    Database& db = reloaded_store.get_db(0);
    ASSERT_TRUE(db.exists("compact_str"));
    ASSERT_TRUE(db.exists("compact_list"));
    ASSERT_TRUE(db.exists("compact_set"));

    // Cleanup
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }
}

TEST_CASE("AOF_TransactionDiscardSafety") {
    const std::string aof_file = "test_tx_aof.aof";
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }

    {
        Store store;
        auto aof = std::make_shared<AOF>(aof_file, AofFsync::Always);
        ASSERT_TRUE(aof->open());
        store.attach_aof(aof);
        ClientContext ctx;

        // Discarded transaction
        store.process_command(ctx, {"MULTI"});
        store.process_command(ctx, {"SET", "discarded_key", "discarded_val"});
        store.process_command(ctx, {"DISCARD"});

        // Executed transaction
        store.process_command(ctx, {"MULTI"});
        store.process_command(ctx, {"SET", "executed_key", "executed_val"});
        store.process_command(ctx, {"EXEC"});
    }

    // Verify recovery: discarded_key must NOT exist, executed_key must exist
    {
        Store reloaded_store;
        AOF aof(aof_file, AofFsync::Always);
        ASSERT_TRUE(aof.load_into(reloaded_store));

        Database& db = reloaded_store.get_db(0);
        ASSERT_FALSE(db.exists("discarded_key"));
        ASSERT_TRUE(db.exists("executed_key"));
    }

    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }
}

TEST_CASE("AOF_BgRewriteAofAutoCreate") {
    const std::string default_aof = "appendonly.aof";
    if (std::filesystem::exists(default_aof)) {
        std::filesystem::remove(default_aof);
    }

    {
        Store store; // No AOF initially attached
        ClientContext ctx;
        store.process_command(ctx, {"SET", "auto_key", "auto_val"});

        std::string reply = store.process_command(ctx, {"BGREWRITEAOF"});
        ASSERT_CONTAINS(reply, "rewriting started");
        ASSERT_TRUE(store.get_aof() != nullptr);
        ASSERT_TRUE(std::filesystem::exists(default_aof));
    }

    // Verify replaying the auto-created AOF restores data
    {
        Store reloaded_store;
        AOF aof(default_aof, AofFsync::EverySec);
        ASSERT_TRUE(aof.load_into(reloaded_store));

        Database& db = reloaded_store.get_db(0);
        ASSERT_TRUE(db.exists("auto_key"));
        auto obj = db.get("auto_key");
        ASSERT_TRUE(obj != nullptr);
        ASSERT_EQ(obj->get_string_value(), "auto_val");
    }

    if (std::filesystem::exists(default_aof)) {
        std::filesystem::remove(default_aof);
    }
}

TEST_CASE("AOF_CrashTruncatedRecovery") {
    const std::string aof_file = "test_truncated_crash.aof";
    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }

    // 1. Write valid commands followed by an incomplete truncated command simulating a crash mid-write
    {
        AOF aof(aof_file, AofFsync::Always);
        ASSERT_TRUE(aof.open());
        aof.log_command({"SET", "k1", "v1"});
        aof.log_command({"SET", "k2", "v2"});
        aof.close();
    }

    // Append partial truncated RESP command "*3\r\n$3\r\nSET\r\n$2\r\nk3\r\n$5\r\n"
    {
        std::ofstream out(aof_file, std::ios::app | std::ios::binary);
        out << "*3\r\n$3\r\nSET\r\n$2\r\nk3\r\n$5\r\nincom"; // Cut off mid-payload
        out.close();
    }

    // 2. Replay into a fresh store - should succeed and recover all complete prior commands
    {
        Store store;
        AOF aof(aof_file, AofFsync::EverySec);
        ASSERT_TRUE(aof.load_into(store));

        Database& db = store.get_db(0);
        ASSERT_TRUE(db.exists("k1"));
        ASSERT_TRUE(db.exists("k2"));
        ASSERT_FALSE(db.exists("k3"));
        ASSERT_EQ(db.get("k1")->get_string_value(), "v1");
        ASSERT_EQ(db.get("k2")->get_string_value(), "v2");
    }

    if (std::filesystem::exists(aof_file)) {
        std::filesystem::remove(aof_file);
    }
}
