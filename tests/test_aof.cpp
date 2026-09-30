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
