#include "test_framework.hpp"
#include "core/internals/sds.hpp"

using rundb::core::internals::SDS;

TEST_CASE("SDS_SSOShortStrings") {
    SDS s("hello");
    ASSERT_TRUE(s.is_sso());
    ASSERT_EQ(s.size(), 5);
    ASSERT_EQ(s.view(), "hello");
    ASSERT_EQ(s.to_string(), "hello");
    ASSERT_FALSE(s.empty());

    // Max SSO capacity is 22 bytes
    std::string s22(22, 'x');
    SDS sso22(s22);
    ASSERT_TRUE(sso22.is_sso());
    ASSERT_EQ(sso22.size(), 22);
    ASSERT_EQ(sso22.view(), s22);
}

TEST_CASE("SDS_HeapAllocatedLongStrings") {
    // 23 bytes exceeds SSO limit
    std::string s23(23, 'a');
    SDS heap_sds(s23);
    ASSERT_FALSE(heap_sds.is_sso());
    ASSERT_EQ(heap_sds.size(), 23);
    ASSERT_GE(heap_sds.capacity(), 23);
    ASSERT_EQ(heap_sds.view(), s23);

    std::string s1000(1000, 'z');
    SDS long_sds(s1000);
    ASSERT_FALSE(long_sds.is_sso());
    ASSERT_EQ(long_sds.size(), 1000);
    ASSERT_EQ(long_sds.view(), s1000);
}

TEST_CASE("SDS_BinarySafety") {
    // String with embedded null bytes
    std::string binary_data = std::string("pre\0fix\0\0suffix", 15);
    SDS s(binary_data);
    ASSERT_EQ(s.size(), 15);
    ASSERT_EQ(std::memcmp(s.data(), binary_data.data(), 15), 0);
}

TEST_CASE("SDS_AppendsAndGeometricGrowth") {
    SDS s;
    ASSERT_TRUE(s.empty());
    ASSERT_TRUE(s.is_sso());

    s.append("hello");
    ASSERT_TRUE(s.is_sso());
    ASSERT_EQ(s.view(), "hello");

    s.append(" world!");
    ASSERT_TRUE(s.is_sso());
    ASSERT_EQ(s.view(), "hello world!");
    ASSERT_EQ(s.size(), 12);

    // Exceed SSO limit by appending
    s.append(" this transitions to heap dynamic string!");
    ASSERT_FALSE(s.is_sso());
    ASSERT_EQ(s.view(), "hello world! this transitions to heap dynamic string!");

    // Multiple appends on heap
    size_t prev_cap = s.capacity();
    for (int i = 0; i < 50; ++i) {
        s.append("1234567890");
    }
    ASSERT_GT(s.capacity(), prev_cap);
    ASSERT_CONTAINS(s.to_string(), "hello world!");
}

TEST_CASE("SDS_PushBackAndClear") {
    SDS s;
    for (char c = 'a'; c <= 'z'; ++c) {
        s.push_back(c);
    }
    ASSERT_EQ(s.size(), 26);
    ASSERT_EQ(s.view(), "abcdefghijklmnopqrstuvwxyz");

    s.clear();
    ASSERT_EQ(s.size(), 0);
    ASSERT_TRUE(s.empty());
}

TEST_CASE("SDS_CopyAndMoveSemantics") {
    std::string payload(100, 'q');
    SDS original(payload);
    ASSERT_FALSE(original.is_sso());

    // Copy construction
    SDS copy(original);
    ASSERT_EQ(copy.view(), payload);
    ASSERT_EQ(original.view(), payload);

    // Move construction
    SDS moved(std::move(original));
    ASSERT_EQ(moved.view(), payload);

    // Copy assignment
    SDS assign_copy;
    assign_copy = copy;
    ASSERT_EQ(assign_copy.view(), payload);

    // Move assignment
    SDS assign_move;
    assign_move = std::move(copy);
    ASSERT_EQ(assign_move.view(), payload);
}

TEST_CASE("SDS_Comparisons") {
    SDS a("apple");
    SDS b("banana");
    SDS a2("apple");

    ASSERT_TRUE(a == a2);
    ASSERT_TRUE(a != b);
    ASSERT_TRUE(a < b);
    ASSERT_TRUE(a == "apple");
    ASSERT_FALSE(a == "banana");
}
