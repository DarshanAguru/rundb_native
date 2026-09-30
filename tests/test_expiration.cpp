#include "test_framework.hpp"
#include "core/database.hpp"
#include "core/expiration.hpp"
#include <chrono>
#include <thread>

using namespace rundb::core;

TEST_CASE("Expiration_PassiveExpiryOnAccess") {
    Database db(0);
    db.set("temp_key", RunDBObject::create_string("temporary_value"));

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Set expiration to 50ms in future
    db.set_expire("temp_key", now + 50);
    ASSERT_TRUE(db.exists("temp_key"));
    ASSERT_GT(db.get_ttl_ms("temp_key"), 0);

    // Sleep for 70ms to allow expiration
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    // Accessing through get() should trigger passive deletion
    auto obj = db.get("temp_key");
    ASSERT_TRUE(obj == nullptr);
    ASSERT_FALSE(db.exists("temp_key"));
    ASSERT_EQ(db.key_count(), 0);
    ASSERT_EQ(db.get_ttl_ms("temp_key"), -2); // -2: key does not exist
}

TEST_CASE("Expiration_PersistRemovesTTL") {
    Database db(0);
    db.set("persist_key", RunDBObject::create_string("value"));

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db.set_expire("persist_key", now + 10000);
    ASSERT_GT(db.get_ttl_ms("persist_key"), 0);
    ASSERT_EQ(db.expires_count(), 1);

    ASSERT_TRUE(db.persist("persist_key"));
    ASSERT_EQ(db.get_ttl_ms("persist_key"), -1); // -1: exists with no TTL
    ASSERT_EQ(db.expires_count(), 0);

    ASSERT_FALSE(db.persist("non_existing"));
}

TEST_CASE("Expiration_ActiveExpireCycle") {
    std::vector<Database> databases;
    databases.emplace_back(0);
    Database& db = databases.back();

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Insert 50 expired keys (expire timestamp in the past)
    for (int i = 0; i < 50; ++i) {
        std::string k = "expired_" + std::to_string(i);
        db.set(k, RunDBObject::create_string("val"));
        db.set_expire(k, now - 1000); // 1 second ago
    }

    // Insert 10 alive keys with long TTL
    for (int i = 0; i < 10; ++i) {
        std::string k = "alive_" + std::to_string(i);
        db.set(k, RunDBObject::create_string("val"));
        db.set_expire(k, now + 100000);
    }

    ASSERT_EQ(db.key_count(), 60);
    ASSERT_EQ(db.expires_count(), 60);

    // Run active expire cycle
    size_t total_purged = 0;
    for (int pass = 0; pass < 5; ++pass) {
        total_purged += Expiration::run_active_cycle(databases);
    }

    ASSERT_GT(total_purged, 0);
    // All 10 alive keys should remain intact
    for (int i = 0; i < 10; ++i) {
        std::string k = "alive_" + std::to_string(i);
        ASSERT_TRUE(db.exists(k));
    }
}
