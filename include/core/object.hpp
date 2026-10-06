#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <memory>
#include "core/internals/sds.hpp"
#include "core/internals/intset.hpp"
#include "core/internals/quicklist.hpp"

namespace rundb::core {

enum class ObjectType : uint8_t {
    String = 0,
    List   = 1,
    Set    = 2,
    ZSet   = 3,
    Hash   = 4
};

enum class ObjectEncoding : uint8_t {
    Raw       = 0,
    InlineInt = 1,
    QuickList = 2,
    IntSet    = 3,
    HashSet   = 4
};

/**
 * @brief High-performance RunDBObject with bit-packed header and proprietary data types.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Compact Header:
 *    - 4 bits for ObjectType (String, List, Set, etc.)
 *    - 4 bits for ObjectEncoding (InlineInt, Raw, QuickList, IntSet, HashSet)
 *    - 24 bits for LRU timestamp (seconds mod 2^24)
 *    Total header overhead is only 4 bytes (32 bits), fitting cleanly in word alignment.
 *
 * 2. Unboxed Inline Integers:
 *    - Integers are stored directly inside the variant as an int64_t without heap allocation.
 *
 * 3. Proprietary Internals:
 *    - Lists use `QuickList` (chunked unrolled doubly linked list)
 *    - Integer Sets use `IntSet` (adaptive binary-search array, 16/32/64 bit)
 *    - General Sets use `std::unordered_set<internals::SDS>`
 *    Total object size is 72 bytes (8-byte header & alignment padding + 64-byte variant).
 */
class RunDBObject {
public:
    using StringData   = internals::SDS;
    using ListData     = internals::QuickList;
    using HashSetData  = std::unordered_set<internals::SDS, internals::TransparentSDSHash, internals::TransparentSDSEqual>;
    using IntSetData   = internals::IntSet;

    using ValueVariant = std::variant<
        int64_t,
        StringData,
        ListData,
        IntSetData,
        HashSetData
    >;

    RunDBObject(ObjectType type, ObjectEncoding enc, ValueVariant val);
    ~RunDBObject() = default;

    RunDBObject(const RunDBObject&) = default;
    RunDBObject(RunDBObject&&) noexcept = default;
    RunDBObject& operator=(const RunDBObject&) = default;
    RunDBObject& operator=(RunDBObject&&) noexcept = default;

    // Factories
    static std::shared_ptr<RunDBObject> create_string(std::string_view val);
    static std::shared_ptr<RunDBObject> create_int(int64_t val);
    static std::shared_ptr<RunDBObject> create_list();
    static std::shared_ptr<RunDBObject> create_set();

    // Inspection
    [[nodiscard]] ObjectType type() const noexcept { return static_cast<ObjectType>(m_header & 0x0F); }
    [[nodiscard]] ObjectEncoding encoding() const noexcept { return static_cast<ObjectEncoding>((m_header >> 4) & 0x0F); }
    [[nodiscard]] uint32_t lru() const noexcept { return (m_header >> 8) & 0x00FFFFFF; }
    void update_lru() noexcept;

    // Value accessors
    [[nodiscard]] std::string get_string_value() const;
    [[nodiscard]] std::string_view get_string_view() const noexcept;
    [[nodiscard]] const internals::SDS* get_sds() const noexcept;
    [[nodiscard]] internals::SDS* get_sds() noexcept;
    [[nodiscard]] int64_t get_int_value() const;
    [[nodiscard]] ListData* get_list() noexcept;
    [[nodiscard]] const ListData* get_list() const noexcept;
    [[nodiscard]] IntSetData* get_intset() noexcept;
    [[nodiscard]] const IntSetData* get_intset() const noexcept;
    [[nodiscard]] HashSetData* get_hashset() noexcept;
    [[nodiscard]] const HashSetData* get_hashset() const noexcept;

    // Set operations
    bool set_add(std::string_view member);
    bool set_remove(std::string_view member);
    [[nodiscard]] bool set_contains(std::string_view member) const;
    [[nodiscard]] size_t set_size() const;

    // Conversions
    void promote_to_hashset();

    // Memory footprint
    [[nodiscard]] size_t memory_bytes() const noexcept;

    // Global LRU clock
    static uint32_t get_lru_clock() noexcept;
    static void update_global_lru_clock() noexcept;

private:
    uint32_t m_header; // 4-bit type, 4-bit enc, 24-bit LRU clock
    ValueVariant m_val;
};

using ObjectPtr = std::shared_ptr<RunDBObject>;

} // namespace rundb::core
