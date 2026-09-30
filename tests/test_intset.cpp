#include "test_framework.hpp"
#include "core/internals/intset.hpp"

using rundb::core::internals::IntSet;

TEST_CASE("IntSet_BasicOperations") {
    IntSet is;
    ASSERT_TRUE(is.empty());
    ASSERT_EQ(is.size(), 0);

    // Add elements
    ASSERT_TRUE(is.add(10));
    ASSERT_TRUE(is.add(20));
    ASSERT_TRUE(is.add(5));

    // Duplicates rejected
    ASSERT_FALSE(is.add(10));
    ASSERT_FALSE(is.add(20));

    ASSERT_EQ(is.size(), 3);
    ASSERT_FALSE(is.empty());

    // Contains
    ASSERT_TRUE(is.contains(5));
    ASSERT_TRUE(is.contains(10));
    ASSERT_TRUE(is.contains(20));
    ASSERT_FALSE(is.contains(15));
    ASSERT_FALSE(is.contains(0));

    // Sorted order: 5, 10, 20
    auto vec = is.to_vector();
    ASSERT_EQ(vec.size(), 3);
    ASSERT_EQ(vec[0], 5);
    ASSERT_EQ(vec[1], 10);
    ASSERT_EQ(vec[2], 20);

    // Remove
    ASSERT_TRUE(is.remove(10));
    ASSERT_FALSE(is.contains(10));
    ASSERT_EQ(is.size(), 2);
    ASSERT_FALSE(is.remove(10)); // already removed

    ASSERT_TRUE(is.remove(5));
    ASSERT_TRUE(is.remove(20));
    ASSERT_TRUE(is.empty());
}

TEST_CASE("IntSet_AdaptiveEncodingUpgrades") {
    IntSet is;

    // Small numbers fit in ENC_INT16 (-32768 to 32767)
    is.add(100);
    is.add(-500);
    is.add(30000);
    ASSERT_EQ(static_cast<int>(is.encoding()), static_cast<int>(IntSet::ENC_INT16));

    // Adding value > 32767 triggers upgrade to ENC_INT32
    is.add(100000);
    ASSERT_EQ(static_cast<int>(is.encoding()), static_cast<int>(IntSet::ENC_INT32));
    ASSERT_TRUE(is.contains(100));
    ASSERT_TRUE(is.contains(-500));
    ASSERT_TRUE(is.contains(30000));
    ASSERT_TRUE(is.contains(100000));

    // Adding 64-bit integer triggers upgrade to ENC_INT64
    int64_t big_val = 5000000000LL;
    is.add(big_val);
    ASSERT_EQ(static_cast<int>(is.encoding()), static_cast<int>(IntSet::ENC_INT64));
    ASSERT_TRUE(is.contains(100));
    ASSERT_TRUE(is.contains(-500));
    ASSERT_TRUE(is.contains(30000));
    ASSERT_TRUE(is.contains(100000));
    ASSERT_TRUE(is.contains(big_val));

    // Verify ordering is maintained after upgrades
    auto v = is.to_vector();
    for (size_t i = 1; i < v.size(); ++i) {
        ASSERT_LT(v[i - 1], v[i]);
    }
}

TEST_CASE("IntSet_CopyAndMove") {
    IntSet orig;
    orig.add(42);
    orig.add(100000);
    orig.add(-7);

    // Copy constructor
    IntSet copy(orig);
    ASSERT_EQ(copy.size(), 3);
    ASSERT_TRUE(copy.contains(42));
    ASSERT_TRUE(copy.contains(100000));
    ASSERT_TRUE(copy.contains(-7));

    // Move constructor
    IntSet moved(std::move(copy));
    ASSERT_EQ(moved.size(), 3);
    ASSERT_TRUE(moved.contains(42));

    // Copy assignment
    IntSet assign_copy;
    assign_copy = orig;
    ASSERT_EQ(assign_copy.size(), 3);
    ASSERT_TRUE(assign_copy.contains(42));

    // Move assignment
    IntSet assign_move;
    assign_move = std::move(moved);
    ASSERT_EQ(assign_move.size(), 3);
    ASSERT_TRUE(assign_move.contains(42));
}

TEST_CASE("IntSet_RandomMemberAndMemoryBytes") {
    IntSet is;
    for (int64_t i = 1; i <= 50; ++i) {
        is.add(i * 10);
    }
    ASSERT_EQ(is.size(), 50);
    ASSERT_GT(is.memory_bytes(), 0);

    auto rand_opt = is.random_member();
    ASSERT_TRUE(rand_opt.has_value());
    ASSERT_TRUE(is.contains(*rand_opt));
}
