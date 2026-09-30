#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>
#include <ostream>

namespace rundb::core::internals {

/**
 * @brief Proprietary Simple Dynamic String (SDS) for RunDB.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Small String Optimization (SSO):
 *    - Strings up to 22 bytes are stored completely in-place inside the object
 *      (24 bytes total size: 1 byte flags + 22 bytes data + 1 byte null terminator).
 *    - This completely eliminates heap allocations for common Redis keys, command names,
 *      and short string values (IDs, counters, status codes, small tokens).
 *
 * 2. Heap-allocated Dynamic Strings:
 *    - For strings > 22 bytes, SDS uses a contiguous cache-line friendly layout:
 *        [Header: 4-byte len, 4-byte alloc] [char data... \0]
 *    - The header is adjacent to the data pointer, so CPU cache prefetching fetches
 *      the length, capacity, and payload in a single 64-byte L1 cache line.
 *    - Fast O(1) strlen: Length is stored explicitly, so binary-safe strings containing
 *      null characters (`\0`) work seamlessly without string scanning.
 *    - 1.5x geometric growth amortizes allocations during APPEND operations.
 */
class SDS {
public:
    static constexpr size_t SSO_CAPACITY = 22;

    SDS() noexcept;
    explicit SDS(std::string_view sv);
    SDS(const char* s, size_t len);
    SDS(const SDS& other);
    SDS(SDS&& other) noexcept;
    ~SDS();

    SDS& operator=(const SDS& other);
    SDS& operator=(SDS&& other) noexcept;

    // Direct access
    [[nodiscard]] const char* data() const noexcept;
    [[nodiscard]] char* data() noexcept;
    [[nodiscard]] const char* c_str() const noexcept { return data(); }
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t length() const noexcept { return size(); }
    [[nodiscard]] size_t capacity() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool is_sso() const noexcept { return (m_flags & 0x01) == 0; }

    // Mutations
    void clear() noexcept;
    void reserve(size_t new_cap);
    void append(std::string_view sv);
    void append(const char* s, size_t len);
    void push_back(char ch);

    // Conversions
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(data(), size()); }
    [[nodiscard]] std::string to_string() const { return std::string(data(), size()); }

    // Comparisons
    bool operator==(const SDS& other) const noexcept;
    bool operator==(std::string_view sv) const noexcept;
    bool operator!=(const SDS& other) const noexcept { return !(*this == other); }
    bool operator<(const SDS& other) const noexcept;

private:
    struct HeapHeader {
        uint32_t len;
        uint32_t alloc;
    };

    struct HeapRep {
        HeapHeader* header;
    };

    struct SsoRep {
        uint8_t len;
        char buf[SSO_CAPACITY + 1]; // +1 for null terminator
    };

    union {
        HeapRep m_heap;
        SsoRep  m_sso;
    };

    uint8_t m_flags; // Bit 0: 0 = SSO, 1 = Heap
};

} // namespace rundb::core::internals
