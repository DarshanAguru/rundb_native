#include "test_framework.hpp"
#include "core/internals/quicklist.hpp"

using rundb::core::internals::QuickList;

TEST_CASE("QuickList_PushPopBoundaries") {
    QuickList ql;
    ASSERT_TRUE(ql.empty());
    ASSERT_EQ(ql.size(), 0);

    // Push back
    ql.push_back("a");
    ql.push_back("b");
    ql.push_back("c");
    ASSERT_EQ(ql.size(), 3);
    ASSERT_FALSE(ql.empty());

    // Push front
    ql.push_front("front");
    ASSERT_EQ(ql.size(), 4);

    // Pop front
    auto popped_front = ql.pop_front();
    ASSERT_TRUE(popped_front.has_value());
    ASSERT_EQ(*popped_front, "front");
    ASSERT_EQ(ql.size(), 3);

    // Pop back
    auto popped_back = ql.pop_back();
    ASSERT_TRUE(popped_back.has_value());
    ASSERT_EQ(*popped_back, "c");
    ASSERT_EQ(ql.size(), 2);

    // Pop remaining
    ASSERT_EQ(*ql.pop_front(), "a");
    ASSERT_EQ(*ql.pop_back(), "b");
    ASSERT_TRUE(ql.empty());
    ASSERT_FALSE(ql.pop_front().has_value());
    ASSERT_FALSE(ql.pop_back().has_value());
}

TEST_CASE("QuickList_MultiChunkSplitting") {
    QuickList ql;

    // Insert 100 elements (exceeds single chunk capacity of 32)
    for (int i = 0; i < 100; ++i) {
        ql.push_back("item_" + std::to_string(i));
    }

    ASSERT_EQ(ql.size(), 100);
    ASSERT_GT(ql.node_count(), 1);

    // Random access through chunks
    ASSERT_EQ(ql.at(0).value_or(""), "item_0");
    ASSERT_EQ(ql.at(31).value_or(""), "item_31");
    ASSERT_EQ(ql.at(32).value_or(""), "item_32");
    ASSERT_EQ(ql.at(99).value_or(""), "item_99");

    // Negative indexing
    ASSERT_EQ(ql.at(-1).value_or(""), "item_99");
    ASSERT_EQ(ql.at(-2).value_or(""), "item_98");
    ASSERT_EQ(ql.at(-100).value_or(""), "item_0");

    // Out of bounds
    ASSERT_FALSE(ql.at(100).has_value());
    ASSERT_FALSE(ql.at(-101).has_value());
}

TEST_CASE("QuickList_RangeSlicing") {
    QuickList ql;
    for (int i = 0; i < 10; ++i) {
        ql.push_back(std::to_string(i));
    }

    // Full range [0, -1]
    auto all_items = ql.range(0, -1);
    ASSERT_EQ(all_items.size(), 10);
    for (size_t i = 0; i < 10; ++i) {
        ASSERT_EQ(all_items[i], std::to_string(i));
    }

    // Sub-range [2, 5]
    auto sub = ql.range(2, 5);
    ASSERT_EQ(sub.size(), 4);
    ASSERT_EQ(sub[0], "2");
    ASSERT_EQ(sub[1], "3");
    ASSERT_EQ(sub[2], "4");
    ASSERT_EQ(sub[3], "5");

    // Negative range [-3, -1]
    auto neg_sub = ql.range(-3, -1);
    ASSERT_EQ(neg_sub.size(), 3);
    ASSERT_EQ(neg_sub[0], "7");
    ASSERT_EQ(neg_sub[1], "8");
    ASSERT_EQ(neg_sub[2], "9");

    // Invalid start > stop
    auto empty_range = ql.range(5, 2);
    ASSERT_TRUE(empty_range.empty());
}

TEST_CASE("QuickList_CopyAndMove") {
    QuickList orig;
    for (int i = 0; i < 50; ++i) {
        orig.push_back("val_" + std::to_string(i));
    }

    // Copy constructor
    QuickList copy(orig);
    ASSERT_EQ(copy.size(), orig.size());
    ASSERT_EQ(copy.at(0).value_or(""), orig.at(0).value_or(""));
    ASSERT_EQ(copy.at(49).value_or(""), orig.at(49).value_or(""));

    // Move constructor
    QuickList moved(std::move(copy));
    ASSERT_EQ(moved.size(), 50);
    ASSERT_TRUE(copy.empty());

    // Copy assignment
    QuickList assign_copy;
    assign_copy = orig;
    ASSERT_EQ(assign_copy.size(), 50);

    // Move assignment
    QuickList assign_move;
    assign_move = std::move(moved);
    ASSERT_EQ(assign_move.size(), 50);
    ASSERT_TRUE(moved.empty());
}
