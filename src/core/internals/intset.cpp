#include "core/internals/intset.hpp"
#include "core/internals/run_alloc.hpp"
#include <cstdlib>
#include <cstring>
#include <new>
#include <limits>
#include <random>
#include <stdexcept>

namespace rundb::core::internals {

using namespace run_alloc;

IntSet::IntSet()
    : m_encoding(ENC_INT16), m_length(0), m_capacity(0), m_data(nullptr) {}

IntSet::~IntSet() {
    if (m_data) {
        run_free(m_data);
        m_data = nullptr;
    }
}

IntSet::IntSet(const IntSet& other)
    : m_encoding(other.m_encoding), m_length(other.m_length), m_capacity(other.m_length) {
    if (m_capacity > 0) {
        size_t bytes = static_cast<size_t>(m_capacity) * m_encoding;
        m_data = static_cast<uint8_t*>(run_malloc(bytes));
        std::memcpy(m_data, other.m_data, static_cast<size_t>(m_length) * m_encoding);
    } else {
        m_data = nullptr;
    }
}

IntSet::IntSet(IntSet&& other) noexcept
    : m_encoding(other.m_encoding), m_length(other.m_length),
      m_capacity(other.m_capacity), m_data(other.m_data) {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
}

IntSet& IntSet::operator=(const IntSet& other) {
    if (this == &other) return *this;
    if (m_data) run_free(m_data);
    m_encoding = other.m_encoding;
    m_length = other.m_length;
    m_capacity = other.m_length;
    if (m_capacity > 0) {
        size_t bytes = static_cast<size_t>(m_capacity) * m_encoding;
        m_data = static_cast<uint8_t*>(run_malloc(bytes));
        std::memcpy(m_data, other.m_data, static_cast<size_t>(m_length) * m_encoding);
    } else {
        m_data = nullptr;
    }
    return *this;
}

IntSet& IntSet::operator=(IntSet&& other) noexcept {
    if (this == &other) return *this;
    if (m_data) run_free(m_data);
    m_encoding = other.m_encoding;
    m_length = other.m_length;
    m_capacity = other.m_capacity;
    m_data = other.m_data;
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
    return *this;
}

IntSet::Encoding IntSet::value_encoding(int64_t val) noexcept {
    if (val >= std::numeric_limits<int16_t>::min() && val <= std::numeric_limits<int16_t>::max()) {
        return ENC_INT16;
    }
    if (val >= std::numeric_limits<int32_t>::min() && val <= std::numeric_limits<int32_t>::max()) {
        return ENC_INT32;
    }
    return ENC_INT64;
}

int64_t IntSet::get_at(size_t pos, Encoding enc) const noexcept {
    const uint8_t* ptr = m_data + (pos * enc);
    if (enc == ENC_INT16) {
        int16_t v;
        std::memcpy(&v, ptr, sizeof(int16_t));
        return v;
    } else if (enc == ENC_INT32) {
        int32_t v;
        std::memcpy(&v, ptr, sizeof(int32_t));
        return v;
    } else {
        int64_t v;
        std::memcpy(&v, ptr, sizeof(int64_t));
        return v;
    }
}

void IntSet::set_at(size_t pos, int64_t val, Encoding enc) noexcept {
    uint8_t* ptr = m_data + (pos * enc);
    if (enc == ENC_INT16) {
        auto v = static_cast<int16_t>(val);
        std::memcpy(ptr, &v, sizeof(int16_t));
    } else if (enc == ENC_INT32) {
        auto v = static_cast<int32_t>(val);
        std::memcpy(ptr, &v, sizeof(int32_t));
    } else {
        std::memcpy(ptr, &val, sizeof(int64_t));
    }
}

bool IntSet::search(int64_t val, size_t& pos) const noexcept {
    if (m_length == 0) {
        pos = 0;
        return false;
    }

    // Quick boundary checks
    if (val > get_at(m_length - 1, m_encoding)) {
        pos = m_length;
        return false;
    } else if (val < get_at(0, m_encoding)) {
        pos = 0;
        return false;
    }

    size_t min = 0;
    size_t max = m_length - 1;
    while (max >= min) {
        size_t mid = min + ((max - min) >> 1);
        int64_t cur = get_at(mid, m_encoding);
        if (val > cur) {
            min = mid + 1;
        } else if (val < cur) {
            if (mid == 0) break;
            max = mid - 1;
        } else {
            pos = mid;
            return true;
        }
    }

    pos = min;
    return false;
}

void IntSet::reallocate(uint32_t new_cap) {
    size_t bytes = static_cast<size_t>(new_cap) * m_encoding;
    auto* mem = static_cast<uint8_t*>(run_realloc(m_data, bytes));
    m_data = mem;
    m_capacity = new_cap;
}

void IntSet::upgrade_and_add(int64_t val) {
    Encoding new_enc = value_encoding(val);
    uint32_t new_len = m_length + 1;
    uint32_t new_cap = (m_capacity < new_len) ? new_len : m_capacity;

    size_t new_bytes = static_cast<size_t>(new_cap) * new_enc;
    auto* new_data = static_cast<uint8_t*>(run_malloc(new_bytes));

    // Value will always be either at index 0 or index new_len - 1 because it's out of current encoding range
    bool prepend = (val < 0);
    size_t insert_pos = prepend ? 0 : m_length;

    // Move existing elements to new encoding
    for (int64_t i = static_cast<int64_t>(m_length) - 1; i >= 0; --i) {
        int64_t old_val = get_at(static_cast<size_t>(i), m_encoding);
        size_t dst_idx = prepend ? (static_cast<size_t>(i) + 1) : static_cast<size_t>(i);
        uint8_t* ptr = new_data + (dst_idx * new_enc);
        if (new_enc == ENC_INT32) {
            auto v = static_cast<int32_t>(old_val);
            std::memcpy(ptr, &v, sizeof(int32_t));
        } else {
            std::memcpy(ptr, &old_val, sizeof(int64_t));
        }
    }

    // Insert new value
    uint8_t* ptr = new_data + (insert_pos * new_enc);
    if (new_enc == ENC_INT32) {
        auto v = static_cast<int32_t>(val);
        std::memcpy(ptr, &v, sizeof(int32_t));
    } else {
        std::memcpy(ptr, &val, sizeof(int64_t));
    }

    if (m_data) run_free(m_data);
    m_data = new_data;
    m_encoding = new_enc;
    m_length = new_len;
    m_capacity = new_cap;
}

bool IntSet::add(int64_t val) {
    Encoding val_enc = value_encoding(val);
    if (val_enc > m_encoding) {
        upgrade_and_add(val);
        return true;
    }

    size_t pos = 0;
    if (search(val, pos)) {
        return false; // Already present
    }

    if (m_length >= m_capacity) {
        uint32_t new_cap = (m_capacity == 0) ? 8 : (m_capacity + (m_capacity >> 1) + 4);
        reallocate(new_cap);
    }

    // Shift elements right
    if (pos < m_length) {
        uint8_t* src = m_data + (pos * m_encoding);
        uint8_t* dst = m_data + ((pos + 1) * m_encoding);
        size_t bytes_to_move = (m_length - pos) * m_encoding;
        std::memmove(dst, src, bytes_to_move);
    }

    set_at(pos, val, m_encoding);
    m_length++;
    return true;
}

bool IntSet::remove(int64_t val) {
    Encoding val_enc = value_encoding(val);
    if (val_enc > m_encoding) return false;

    size_t pos = 0;
    if (!search(val, pos)) return false;

    // Shift elements left
    if (pos < m_length - 1) {
        uint8_t* dst = m_data + (pos * m_encoding);
        uint8_t* src = m_data + ((pos + 1) * m_encoding);
        size_t bytes_to_move = (m_length - pos - 1) * m_encoding;
        std::memmove(dst, src, bytes_to_move);
    }

    m_length--;
    return true;
}

bool IntSet::contains(int64_t val) const noexcept {
    Encoding val_enc = value_encoding(val);
    if (val_enc > m_encoding) return false;
    size_t pos = 0;
    return search(val, pos);
}

int64_t IntSet::get(size_t pos) const {
    if (pos >= m_length) throw std::out_of_range("IntSet index out of bounds");
    return get_at(pos, m_encoding);
}

std::vector<int64_t> IntSet::to_vector() const {
    std::vector<int64_t> vec;
    vec.reserve(m_length);
    for (size_t i = 0; i < m_length; ++i) {
        vec.push_back(get_at(i, m_encoding));
    }
    return vec;
}

std::optional<int64_t> IntSet::random_member() const {
    if (m_length == 0) return std::nullopt;
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, m_length - 1);
    return get_at(dist(gen), m_encoding);
}

size_t IntSet::memory_bytes() const noexcept {
    return sizeof(IntSet) + (static_cast<size_t>(m_capacity) * m_encoding);
}

} // namespace rundb::core::internals
