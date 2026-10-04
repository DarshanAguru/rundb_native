#include "test_framework.hpp"
#include "core/snapshot.hpp"
#include "core/store.hpp"
#include "core/evaluator.hpp"
#include "argparse.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace rundb::core;

TEST_CASE("Snapshot_ParseInterval") {
    // Minutes
    ASSERT_EQ(SnapshotManager::parse_interval("1M"), 60);
    ASSERT_EQ(SnapshotManager::parse_interval("1m"), 60);
    ASSERT_EQ(SnapshotManager::parse_interval("5M"), 300);
    ASSERT_EQ(SnapshotManager::parse_interval(" 10M "), 600);

    // Seconds
    ASSERT_EQ(SnapshotManager::parse_interval("1S"), 1);
    ASSERT_EQ(SnapshotManager::parse_interval("30s"), 30);
    ASSERT_EQ(SnapshotManager::parse_interval("45S"), 45);

    // Hours
    ASSERT_EQ(SnapshotManager::parse_interval("1H"), 3600);
    ASSERT_EQ(SnapshotManager::parse_interval("2h"), 7200);

    // Days
    ASSERT_EQ(SnapshotManager::parse_interval("1D"), 86400);

    // Plain seconds (digits without suffix)
    ASSERT_EQ(SnapshotManager::parse_interval("120"), 120);

    // Disabled / zero
    ASSERT_EQ(SnapshotManager::parse_interval("0"), 0);
    ASSERT_EQ(SnapshotManager::parse_interval(""), 0);
    ASSERT_EQ(SnapshotManager::parse_interval("none"), 0);
    ASSERT_EQ(SnapshotManager::parse_interval("disabled"), 0);

    // Invalid format should throw
    ASSERT_THROWS(SnapshotManager::parse_interval("invalid"), std::runtime_error);
    ASSERT_THROWS(SnapshotManager::parse_interval("-1M"), std::runtime_error);
    ASSERT_THROWS(SnapshotManager::parse_interval("M"), std::runtime_error);
    ASSERT_THROWS(SnapshotManager::parse_interval("10X"), std::runtime_error);
}

TEST_CASE("Snapshot_SaveAndLoadWithCompression") {
    const std::string test_snap = "test_dump.rdb";
    std::filesystem::remove(test_snap);

    {
        Store store(0, EvictionPolicy::NoEviction, 4);
        auto& db0 = store.get_db(0);
        auto& db1 = store.get_db(1);

        // String keys
        db0.set("str_key1", RunDBObject::create_string("hello_world_value_123456789"));
        db0.set("str_key2", RunDBObject::create_string("another_long_string_that_repeats_again_and_again_for_compression"));
        db0.set("str_key3", RunDBObject::create_string("another_long_string_that_repeats_again_and_again_for_compression"));

        // List keys
        auto list_obj = RunDBObject::create_list();
        auto* ql_init = list_obj->get_list();
        for (int i = 0; i < 50; ++i) {
            ql_init->push_back("item_" + std::to_string(i));
        }
        db0.set("list_key", std::move(list_obj));

        // Set keys
        auto set_obj = RunDBObject::create_set();
        for (int i = 0; i < 50; ++i) {
            set_obj->set_add("member_" + std::to_string(i));
        }
        db0.set("set_key", std::move(set_obj));

        // Key with TTL (1 hour from now)
        db0.set("ttl_key", RunDBObject::create_string("will_expire_later"));
        db0.set_expire("ttl_key", 9999999999000ULL);

        // In DB 1
        db1.set("db1_key", RunDBObject::create_string("db1_isolated_value"));

        SnapshotManager snap(test_snap, 0);
        ASSERT_TRUE(snap.save(store));
        ASSERT_TRUE(std::filesystem::exists(test_snap));

        size_t file_sz = std::filesystem::file_size(test_snap);
        ASSERT_TRUE(file_sz > 40); // Valid header + compressed data
    }

    // Now restore into a completely fresh Store
    {
        Store store2(0, EvictionPolicy::NoEviction, 4);
        SnapshotManager snap(test_snap, 0);
        size_t restored = snap.load_into(store2);
        ASSERT_EQ(restored, 7); // 6 keys in db0 + 1 key in db1

        auto& db0 = store2.get_db(0);
        auto& db1 = store2.get_db(1);

        // Verify Strings
        auto o1 = db0.get("str_key1");
        ASSERT_TRUE(o1 != nullptr);
        ASSERT_EQ(o1->get_string_value(), "hello_world_value_123456789");

        auto o2 = db0.get("str_key2");
        ASSERT_TRUE(o2 != nullptr);
        ASSERT_EQ(o2->get_string_value(), "another_long_string_that_repeats_again_and_again_for_compression");

        // Verify List
        auto o_list = db0.get("list_key");
        ASSERT_TRUE(o_list != nullptr);
        ASSERT_TRUE(o_list->type() == ObjectType::List);
        auto* ql = o_list->get_list();
        ASSERT_TRUE(ql != nullptr);
        ASSERT_EQ(ql->size(), 50);
        ASSERT_EQ(ql->at(0).value(), "item_0");
        ASSERT_EQ(ql->at(49).value(), "item_49");

        // Verify Set
        auto o_set = db0.get("set_key");
        ASSERT_TRUE(o_set != nullptr);
        ASSERT_TRUE(o_set->type() == ObjectType::Set);
        ASSERT_TRUE(o_set->set_contains("member_0"));
        ASSERT_TRUE(o_set->set_contains("member_49"));
        ASSERT_FALSE(o_set->set_contains("non_existent"));

        // Verify TTL preserved
        int64_t ttl = db0.get_ttl_ms("ttl_key");
        ASSERT_TRUE(ttl > 0);

        // Verify DB1 key
        auto o_db1 = db1.get("db1_key");
        ASSERT_TRUE(o_db1 != nullptr);
        ASSERT_EQ(o_db1->get_string_value(), "db1_isolated_value");
    }

    std::filesystem::remove(test_snap);
}

TEST_CASE("Snapshot_BgSaveAsynchronous") {
    const std::string test_snap = "test_bgsave.rdb";
    std::filesystem::remove(test_snap);

    Store store(0, EvictionPolicy::NoEviction, 2);
    store.get_db(0).set("bg_key", RunDBObject::create_string("bg_val"));

    SnapshotManager snap(test_snap, 0);
    ASSERT_FALSE(snap.bgsave_in_progress());

    ASSERT_TRUE(snap.bgsave(store));
    snap.wait_for_bg_save();

    ASSERT_FALSE(snap.bgsave_in_progress());
    ASSERT_EQ(snap.last_bgsave_status(), "ok");
    ASSERT_TRUE(snap.exists());

    std::filesystem::remove(test_snap);
}

TEST_CASE("Snapshot_Commands") {
    const std::string test_snap = "test_cmd_dump.rdb";
    std::filesystem::remove(test_snap);

    Store store(0, EvictionPolicy::NoEviction, 2);
    auto snap = std::make_shared<SnapshotManager>(test_snap, 0);
    store.attach_snapshot_manager(snap);

    ClientContext ctx;

    // SAVE command
    store.get_db(0).set("k1", RunDBObject::create_string("v1"));
    std::string save_res = store.process_command(ctx, {"SAVE"});
    ASSERT_EQ(save_res, "+OK\r\n");
    ASSERT_TRUE(std::filesystem::exists(test_snap));

    // LASTSAVE command
    std::string lastsave_res = store.process_command(ctx, {"LASTSAVE"});
    ASSERT_TRUE(lastsave_res.size() > 3);
    ASSERT_TRUE(lastsave_res.front() == ':');

    // BGSAVE command
    std::string bgsave_res = store.process_command(ctx, {"BGSAVE"});
    ASSERT_EQ(bgsave_res, "+Background saving started\r\n");
    snap->wait_for_bg_save();

    // INFO command persistence section
    std::string info_res = store.process_command(ctx, {"INFO"});
    ASSERT_TRUE(info_res.find("rdb_last_bgsave_status:ok") != std::string::npos);
    ASSERT_TRUE(info_res.find("rdb_changes_since_last_save:") != std::string::npos);

    // CONFIG GET / SET snapshot-interval
    std::string cset_res = store.process_command(ctx, {"CONFIG", "SET", "snapshot-interval", "1M"});
    ASSERT_EQ(cset_res, "+OK\r\n");
    ASSERT_EQ(snap->interval_sec(), 60);

    std::string cget_res = store.process_command(ctx, {"CONFIG", "GET", "snapshot-interval"});
    ASSERT_TRUE(cget_res.find("1M") != std::string::npos);

    // CONFIG GET / SET save
    std::string cset_save = store.process_command(ctx, {"CONFIG", "SET", "save", "30S"});
    ASSERT_EQ(cset_save, "+OK\r\n");
    ASSERT_EQ(snap->interval_sec(), 30);

    std::filesystem::remove(test_snap);
}
