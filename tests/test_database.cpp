#include "test_framework.hpp"
#include "core/object.hpp"
#include "core/database.hpp"

using namespace rundb::core;

TEST_CASE("Database_ObjectCreationAndTypes") {
    auto str_obj = RunDBObject::create_string("hello");
    ASSERT_EQ(static_cast<int>(str_obj->type()), static_cast<int>(ObjectType::String));
    ASSERT_EQ(str_obj->get_string_value(), "hello");

    auto int_obj = RunDBObject::create_int(12345);
    ASSERT_EQ(static_cast<int>(int_obj->type()), static_cast<int>(ObjectType::String));
    ASSERT_EQ(static_cast<int>(int_obj->encoding()), static_cast<int>(ObjectEncoding::InlineInt));
    ASSERT_EQ(int_obj->get_int_value(), 12345);
    ASSERT_EQ(int_obj->get_string_value(), "12345");

    auto list_obj = RunDBObject::create_list();
    ASSERT_EQ(static_cast<int>(list_obj->type()), static_cast<int>(ObjectType::List));
    ASSERT_TRUE(list_obj->get_list() != nullptr);

    auto set_obj = RunDBObject::create_set();
    ASSERT_EQ(static_cast<int>(set_obj->type()), static_cast<int>(ObjectType::Set));
}

TEST_CASE("Database_SetAdaptivePromotion") {
    auto set_obj = RunDBObject::create_set();
    ASSERT_EQ(static_cast<int>(set_obj->encoding()), static_cast<int>(ObjectEncoding::IntSet));

    // Adding integer strings keeps IntSet encoding
    ASSERT_TRUE(set_obj->set_add("100"));
    ASSERT_TRUE(set_obj->set_add("200"));
    ASSERT_EQ(static_cast<int>(set_obj->encoding()), static_cast<int>(ObjectEncoding::IntSet));
    ASSERT_EQ(set_obj->set_size(), 2);
    ASSERT_TRUE(set_obj->set_contains("100"));
    ASSERT_TRUE(set_obj->set_contains("200"));
    ASSERT_FALSE(set_obj->set_contains("300"));

    // Adding a non-integer string triggers promotion to HashSet
    ASSERT_TRUE(set_obj->set_add("non_numeric_token"));
    ASSERT_EQ(static_cast<int>(set_obj->encoding()), static_cast<int>(ObjectEncoding::HashSet));
    ASSERT_EQ(set_obj->set_size(), 3);
    ASSERT_TRUE(set_obj->set_contains("100"));
    ASSERT_TRUE(set_obj->set_contains("200"));
    ASSERT_TRUE(set_obj->set_contains("non_numeric_token"));

    // Removal on HashSet
    ASSERT_TRUE(set_obj->set_remove("100"));
    ASSERT_FALSE(set_obj->set_contains("100"));
    ASSERT_EQ(set_obj->set_size(), 2);
}

TEST_CASE("Database_CRUDOperations") {
    Database db(0);
    ASSERT_EQ(db.key_count(), 0);
    ASSERT_FALSE(db.exists("key1"));

    // Set & Get
    db.set("key1", RunDBObject::create_string("val1"));
    ASSERT_TRUE(db.exists("key1"));
    ASSERT_EQ(db.key_count(), 1);

    auto obj = db.get("key1");
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->get_string_value(), "val1");

    // Overwrite
    db.set("key1", RunDBObject::create_string("val1_updated"));
    ASSERT_EQ(db.get("key1")->get_string_value(), "val1_updated");
    ASSERT_EQ(db.key_count(), 1);

    // Delete
    ASSERT_TRUE(db.del("key1"));
    ASSERT_FALSE(db.exists("key1"));
    ASSERT_EQ(db.key_count(), 0);
    ASSERT_FALSE(db.del("key1")); // already deleted
}

TEST_CASE("Database_FlushKeyspace") {
    Database db(1);
    for (int i = 0; i < 20; ++i) {
        db.set("k" + std::to_string(i), RunDBObject::create_string("v"));
    }
    ASSERT_EQ(db.key_count(), 20);

    db.flush();
    ASSERT_EQ(db.key_count(), 0);
    ASSERT_EQ(db.expires_count(), 0);
}

TEST_CASE("Database_Sampling") {
    Database db(0);
    for (int i = 0; i < 50; ++i) {
        db.set("sample_key_" + std::to_string(i), RunDBObject::create_string("v"));
    }

    auto samples = db.sample_keys(10);
    ASSERT_EQ(samples.size(), 10);
    for (const auto& k : samples) {
        ASSERT_TRUE(db.exists(k));
    }
}
