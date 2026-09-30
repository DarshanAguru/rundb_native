#include "test_framework.hpp"
#include "core/eviction.hpp"
#include "core/evict_pool.hpp"
#include "core/database.hpp"
#include <thread>
#include <chrono>

using namespace rundb::core;

TEST_CASE("Eviction_PolicyParsing") {
    ASSERT_EQ(static_cast<int>(Eviction::parse_policy("allkeys-lru")), static_cast<int>(EvictionPolicy::AllKeysLRU));
    ASSERT_EQ(static_cast<int>(Eviction::parse_policy("volatile-lru")), static_cast<int>(EvictionPolicy::VolatileLRU));
    ASSERT_EQ(static_cast<int>(Eviction::parse_policy("allkeys-random")), static_cast<int>(EvictionPolicy::AllKeysRandom));
    ASSERT_EQ(static_cast<int>(Eviction::parse_policy("volatile-random")), static_cast<int>(EvictionPolicy::VolatileRandom));
    ASSERT_EQ(static_cast<int>(Eviction::parse_policy("noeviction")), static_cast<int>(EvictionPolicy::NoEviction));

    ASSERT_EQ(Eviction::policy_to_string(EvictionPolicy::AllKeysLRU), "allkeys-lru");
    ASSERT_EQ(Eviction::policy_to_string(EvictionPolicy::NoEviction), "noeviction");
}

TEST_CASE("Eviction_EvictPoolRanking") {
    EvictPool pool;
    ASSERT_TRUE(pool.empty());

    pool.insert(100, "key_a", 0);
    pool.insert(500, "key_b", 0);
    pool.insert(300, "key_c", 0);

    ASSERT_EQ(pool.size(), size_t(3));

    // Highest idle time should be evicted first (key_b with 500)
    auto top1 = pool.pop_best();
    ASSERT_TRUE(top1.has_value());
    ASSERT_EQ(top1->key, "key_b");
    ASSERT_EQ(top1->idle_time, uint32_t(500));

    // Next is key_c with 300
    auto top2 = pool.pop_best();
    ASSERT_TRUE(top2.has_value());
    ASSERT_EQ(top2->key, "key_c");

    // Next is key_a with 100
    auto top3 = pool.pop_best();
    ASSERT_TRUE(top3.has_value());
    ASSERT_EQ(top3->key, "key_a");

    ASSERT_TRUE(pool.empty());
}

TEST_CASE("Eviction_AllKeysLRUEviction") {
    std::vector<Database> databases;
    databases.emplace_back(0);
    Database& db = databases.back();
    EvictPool pool;

    size_t base_mem = Eviction::get_used_memory();

    for (int i = 0; i < 20; ++i) {
        auto list_obj = RunDBObject::create_list();
        list_obj->get_list()->push_back("item_" + std::to_string(i));
        db.set("key_" + std::to_string(i), list_obj);
    }

    size_t current_mem = Eviction::get_used_memory();
    ASSERT_GT(current_mem, base_mem);
    size_t before_keys = db.key_count();
    ASSERT_EQ(before_keys, size_t(20));

    // Target maxmemory halfway between base_mem and current_mem
    size_t target_maxmemory = (base_mem + current_mem) / 2;

    size_t evicted = Eviction::perform_eviction(databases, pool, EvictionPolicy::AllKeysLRU, target_maxmemory);

    ASSERT_GT(evicted, size_t(0));
    ASSERT_LT(db.key_count(), before_keys);
    ASSERT_LE(Eviction::get_used_memory(), target_maxmemory);
}

TEST_CASE("Eviction_VolatileEvictionOnlyTargetsKeysWithTTL") {
    std::vector<Database> databases;
    databases.emplace_back(0);
    Database& db = databases.back();
    EvictPool pool;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    size_t base_mem = Eviction::get_used_memory();

    // 5 keys without TTL (persistent)
    for (int i = 0; i < 5; ++i) {
        auto list_obj = RunDBObject::create_list();
        list_obj->get_list()->push_back("permanent");
        db.set("perm_" + std::to_string(i), list_obj);
    }

    // 5 keys with TTL
    for (int i = 0; i < 5; ++i) {
        std::string k = "volatile_" + std::to_string(i);
        auto list_obj = RunDBObject::create_list();
        list_obj->get_list()->push_back("volatile_val");
        db.set(k, list_obj);
        db.set_expire(k, now + 100000);
    }

    size_t current_mem = Eviction::get_used_memory();
    ASSERT_GT(current_mem, base_mem);

    // Target maxmemory to trigger eviction of volatile keys
    size_t target_maxmemory = current_mem - 500;

    size_t evicted = Eviction::perform_eviction(databases, pool, EvictionPolicy::VolatileLRU, target_maxmemory);
    ASSERT_GT(evicted, size_t(0));

    // All permanent keys must survive!
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(db.exists("perm_" + std::to_string(i)));
    }
}
