#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include "core/internals/sds.hpp"

namespace rundb::core::internals {

/**
 * @brief Proprietary Chunked Unrolled Doubly-Linked List (QuickList).
 *
 * DESIGN & WHY IT WORKS:
 * 1. Hybrid Cache-Conscious Architecture:
 *    - Pure linked lists suffer from pointer chasing: each node causes an L1 cache miss.
 *    - Pure dynamic vectors require expensive O(N) reallocations and front shifts.
 *    - QuickList combines both: A doubly-linked list of fixed-capacity chunks (e.g., 32 elements).
 *
 * 2. O(1) Push/Pop at Both Ends:
 *    - LPUSH adds to the head chunk. When full, a new chunk node is linked in O(1).
 *    - RPUSH adds to the tail chunk.
 *    - LPOP / RPOP drain the boundaries in O(1) without shifting any other chunks.
 *
 * 3. Fast Random Access & Slicing:
 *    - Indexing skips whole chunks (idx / 32) before doing direct O(1) array indexing.
 *    - LRANGE slices contiguous chunks rapidly with minimal iterator overhead.
 */
class QuickList {
public:
    static constexpr size_t CHUNK_CAPACITY = 32;

    QuickList();
    ~QuickList();

    QuickList(const QuickList& other);
    QuickList(QuickList&& other) noexcept;
    QuickList& operator=(const QuickList& other);
    QuickList& operator=(QuickList&& other) noexcept;

    // Boundary mutations (O(1))
    void push_front(std::string_view val);
    void push_back(std::string_view val);
    std::optional<std::string> pop_front();
    std::optional<std::string> pop_back();

    // Inspection
    [[nodiscard]] size_t size() const noexcept { return m_total_elements; }
    [[nodiscard]] bool empty() const noexcept { return m_total_elements == 0; }
    [[nodiscard]] size_t node_count() const noexcept { return m_node_count; }
    [[nodiscard]] size_t memory_bytes() const noexcept;

    // Random access & ranges
    [[nodiscard]] std::optional<std::string> at(int64_t index) const;
    [[nodiscard]] std::vector<std::string> range(int64_t start, int64_t stop) const;

private:
    struct Node {
        SDS items[CHUNK_CAPACITY];
        uint16_t start_idx{0}; // Start offset inside items
        uint16_t count{0};     // Active items count
        Node* prev{nullptr};
        Node* next{nullptr};

        [[nodiscard]] bool is_full() const noexcept { return (start_idx + count) >= CHUNK_CAPACITY; }
        [[nodiscard]] bool is_empty() const noexcept { return count == 0; }
        [[nodiscard]] size_t memory_bytes() const noexcept;

        static void* operator new(size_t size);
        static void operator delete(void* ptr) noexcept;
    };

    Node* m_head{nullptr};
    Node* m_tail{nullptr};
    size_t m_total_elements{0};
    size_t m_node_count{0};

    void clear() noexcept;
};

} // namespace rundb::core::internals
