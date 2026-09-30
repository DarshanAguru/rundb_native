#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include "core/object.hpp"

namespace rundb::core {

/**
 * @brief Partitioned Key-Value Database with passive expiration and sampling.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Independent Expires Table:
 *    - Expiry timestamps are kept in a separate lookup map `m_expires`, so keys without
 *      a TTL incur zero memory penalty.
 * 2. Passive (Lazy) Expiration:
 *    - Whenever any key is accessed (`get`), `is_expired()` checks if current time > expiry.
 *    - If expired, it is immediately deleted and treated as a cache miss, ensuring clients
 *      never observe stale values.
 * 3. O(1) Random Sampling:
 *    - Used by the LRU candidate pool and active expiration algorithms to audit keys.
 */
class Database {
public:
    explicit Database(int db_id = 0) : m_id(db_id) {}
    ~Database() = default;

    // Core operations
    void set(std::string_view key, ObjectPtr val);
    [[nodiscard]] ObjectPtr get(std::string_view key);
    [[nodiscard]] ObjectPtr get_no_touch(std::string_view key);
    bool del(std::string_view key);
    [[nodiscard]] bool exists(std::string_view key);
    void flush() noexcept;

    // Expiration
    bool set_expire(std::string_view key, uint64_t expire_time_ms);
    bool persist(std::string_view key);
    [[nodiscard]] int64_t get_ttl_ms(std::string_view key);
    [[nodiscard]] bool is_expired(std::string_view key);

    // Eviction & sampling
    [[nodiscard]] std::vector<std::string> sample_keys(size_t count);
    [[nodiscard]] std::vector<std::string> sample_expires(size_t count);

    // Metrics
    [[nodiscard]] size_t key_count() const noexcept { return m_dict.size(); }
    [[nodiscard]] size_t expires_count() const noexcept { return m_expires.size(); }
    [[nodiscard]] int id() const noexcept { return m_id; }

    struct TransparentStringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
        size_t operator()(const char* s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    using DictMap = std::unordered_map<std::string, ObjectPtr, TransparentStringHash, std::equal_to<>>;
    using ExpiresMap = std::unordered_map<std::string, uint64_t, TransparentStringHash, std::equal_to<>>;

    [[nodiscard]] const DictMap& dict() const noexcept { return m_dict; }
    [[nodiscard]] const ExpiresMap& expires() const noexcept { return m_expires; }

private:
    int m_id;
    DictMap m_dict;
    ExpiresMap m_expires; // Key -> Unix timestamp in ms

    [[nodiscard]] static uint64_t now_ms() noexcept;
};

} // namespace rundb::core
