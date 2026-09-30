#include "test_framework.hpp"
#include "core/internals/run_alloc.hpp"
#include <cstring>
#include <cstdint>

using namespace rundb::core::internals::run_alloc;

TEST_CASE("Alloc_BasicMallocFree") {
    size_t before_used = get_used_memory();
    size_t before_blocks = get_used_blocks();

    void* ptr = run_malloc(128);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_GE(run_usable_size(ptr), 128);

    // Verify 16-byte alignment
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    ASSERT_EQ(addr % 16, 0);

    // Verify memory tracking incremented
    ASSERT_GT(get_used_memory(), before_used);
    ASSERT_EQ(get_used_blocks(), before_blocks + 1);

    // Write and read pattern
    std::memset(ptr, 0xAB, 128);
    uint8_t* bytes = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < 128; ++i) {
        ASSERT_EQ(bytes[i], 0xAB);
    }

    run_free(ptr);
    ASSERT_EQ(get_used_blocks(), before_blocks);
    ASSERT_EQ(get_used_memory(), before_used);
}

TEST_CASE("Alloc_CallocZeroInitialized") {
    size_t count = 64;
    size_t elem_size = 4;
    void* ptr = run_calloc(count, elem_size);
    ASSERT_TRUE(ptr != nullptr);

    uint8_t* bytes = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < count * elem_size; ++i) {
        ASSERT_EQ(bytes[i], 0);
    }

    run_free(ptr);
}

TEST_CASE("Alloc_ReallocGrowthAndShrink") {
    size_t initial_size = 64;
    uint8_t* ptr = static_cast<uint8_t*>(run_malloc(initial_size));
    ASSERT_TRUE(ptr != nullptr);

    for (size_t i = 0; i < initial_size; ++i) {
        ptr[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Grow
    size_t grown_size = 256;
    ptr = static_cast<uint8_t*>(run_realloc(ptr, grown_size));
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_GE(run_usable_size(ptr), grown_size);

    // Original data must be preserved
    for (size_t i = 0; i < initial_size; ++i) {
        ASSERT_EQ(ptr[i], static_cast<uint8_t>(i & 0xFF));
    }

    // Shrink
    size_t shrink_size = 32;
    ptr = static_cast<uint8_t*>(run_realloc(ptr, shrink_size));
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_GE(run_usable_size(ptr), shrink_size);

    for (size_t i = 0; i < shrink_size; ++i) {
        ASSERT_EQ(ptr[i], static_cast<uint8_t>(i & 0xFF));
    }

    run_free(ptr);
}

TEST_CASE("Alloc_FreeNullPointerSafe") {
    ASSERT_NO_THROW(run_free(nullptr));
}

TEST_CASE("Alloc_Telemetry") {
    std::string alloc_name = get_allocator_name();
    ASSERT_FALSE(alloc_name.empty());
    ASSERT_NO_THROW(get_resident_memory());
}
