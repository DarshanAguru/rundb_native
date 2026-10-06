#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <optional>

namespace rundb::core::internals {

/**
 * @brief Proprietary Adaptive Compact Integer Set (IntSet).
 *
 * DESIGN & WHY IT WORKS:
 * 1. Adaptive Encoding:
 *    - Integers are encoded using the minimum required bit width:
 *        ENC_INT16: 2 bytes per integer (values in [-32768, 32767])
 *        ENC_INT32: 4 bytes per integer (values in [-2147483648, 2147483647])
 *        ENC_INT64: 8 bytes per integer (full 64-bit signed range)
 *    - An IntSet with 10,000 small IDs takes only 20 KB (compared to 80 KB for 64-bit,
 *      or ~560 KB for std::unordered_set node overheads!).
 *
 * 2. Contiguous Binary Search (O(log N)):
 *    - Elements are stored in strictly ascending sorted order.
 *    - Binary search yields branch-predictable lookups and cache prefetching.
 *
 * 3. Backward Upgrading & Shifting:
 *    - When an inserted integer exceeds the current encoding width, `upgrade_and_add()`
 *      allocates a new buffer sized for the wider encoding and migrates existing elements
 *      from back to front (preserving index alignment), then frees the old buffer.
 *    - Insertion within the same encoding uses `std::memmove` for high-speed hardware block moves.
 */
class IntSet {
public:
    enum Encoding : uint8_t {
        ENC_INT16 = 2,
        ENC_INT32 = 4,
        ENC_INT64 = 8
    };

    IntSet();
    ~IntSet();

    IntSet(const IntSet& other);
    IntSet(IntSet&& other) noexcept;
    IntSet& operator=(const IntSet& other);
    IntSet& operator=(IntSet&& other) noexcept;

    // Core operations
    bool add(int64_t val);
    bool remove(int64_t val);
    [[nodiscard]] bool contains(int64_t val) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return m_length; }
    [[nodiscard]] bool empty() const noexcept { return m_length == 0; }
    [[nodiscard]] Encoding encoding() const noexcept { return m_encoding; }
    [[nodiscard]] size_t memory_bytes() const noexcept;

    // Extraction & iteration
    [[nodiscard]] int64_t get(size_t pos) const;
    [[nodiscard]] std::vector<int64_t> to_vector() const;
    [[nodiscard]] std::optional<int64_t> random_member() const;

private:
    Encoding m_encoding;
    uint32_t m_length;
    uint32_t m_capacity;
    uint8_t* m_data; // Raw contiguous byte buffer

    [[nodiscard]] static Encoding value_encoding(int64_t val) noexcept;
    [[nodiscard]] int64_t get_at(size_t pos, Encoding enc) const noexcept;
    void set_at(size_t pos, int64_t val, Encoding enc) noexcept;

    [[nodiscard]] bool search(int64_t val, size_t& pos) const noexcept;
    void upgrade_and_add(int64_t val);
    void reallocate(uint32_t new_cap);
};

} // namespace rundb::core::internals
