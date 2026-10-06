#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <cstring>

namespace rundb::core::internals {

/**
 * @brief High-performance hash functions for RunDB key indexing and hashing.
 *
 * Provides:
 * - MurmurHash3 (64-bit finalizer, excellent avalanche characteristics)
 * - FNV-1a (64-bit, fast for short strings)
 */
class Hashers {
public:
    // 64-bit MurmurHash3
    [[nodiscard]] static inline uint64_t murmur3_64(std::string_view key, uint64_t seed = 0x5bd1e995) noexcept {
        const uint64_t m = 0xc6a4a7935bd1e995ULL;
        const int r = 47;
        uint64_t h = seed ^ (key.size() * m);
        size_t nblocks = key.size() / 8;

        for (size_t i = 0; i < nblocks; ++i) {
            uint64_t k = 0;
            std::memcpy(&k, key.data() + (i * 8), sizeof(uint64_t));
            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;
        }

        const auto* data2 = reinterpret_cast<const uint8_t*>(key.data() + (nblocks * 8));
        switch (key.size() & 7) {
            case 7: h ^= static_cast<uint64_t>(data2[6]) << 48; [[fallthrough]];
            case 6: h ^= static_cast<uint64_t>(data2[5]) << 40; [[fallthrough]];
            case 5: h ^= static_cast<uint64_t>(data2[4]) << 32; [[fallthrough]];
            case 4: h ^= static_cast<uint64_t>(data2[3]) << 24; [[fallthrough]];
            case 3: h ^= static_cast<uint64_t>(data2[2]) << 16; [[fallthrough]];
            case 2: h ^= static_cast<uint64_t>(data2[1]) << 8;  [[fallthrough]];
            case 1: h ^= static_cast<uint64_t>(data2[0]);
                    h *= m;
        }

        h ^= h >> r;
        h *= m;
        h ^= h >> r;

        return h;
    }

    // 64-bit FNV-1a
    [[nodiscard]] static constexpr uint64_t fnv1a_64(std::string_view key) noexcept {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : key) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

} // namespace rundb::core::internals
