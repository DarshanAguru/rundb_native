#include "core/internals/sds.hpp"
#include "core/internals/run_alloc.hpp"
#include <new>
#include <cstring>

namespace rundb::core::internals {

using namespace run_alloc;

SDS::SDS() noexcept : m_flags(0) {
    m_sso.len = 0;
    m_sso.buf[0] = '\0';
}

SDS::SDS(std::string_view sv) : m_flags(0) {
    if (sv.size() <= SSO_CAPACITY) {
        m_sso.len = static_cast<uint8_t>(sv.size());
        if (!sv.empty()) {
            std::memcpy(m_sso.buf, sv.data(), sv.size());
        }
        m_sso.buf[sv.size()] = '\0';
    } else {
        m_flags = 1;
        size_t alloc_cap = sv.size();
        size_t total_bytes = sizeof(HeapHeader) + alloc_cap + 1;
        auto* mem = static_cast<char*>(run_malloc(total_bytes));
        auto* hdr = reinterpret_cast<HeapHeader*>(mem);
        hdr->len = static_cast<uint32_t>(sv.size());
        hdr->alloc = static_cast<uint32_t>(alloc_cap);
        std::memcpy(mem + sizeof(HeapHeader), sv.data(), sv.size());
        mem[sizeof(HeapHeader) + sv.size()] = '\0';
        m_heap.header = hdr;
    }
}

SDS::SDS(const char* s, size_t len) : SDS(std::string_view(s, len)) {}

SDS::SDS(const SDS& other) : m_flags(other.m_flags) {
    if (is_sso()) {
        m_sso = other.m_sso;
    } else {
        size_t alloc_cap = other.m_heap.header->alloc;
        size_t len = other.m_heap.header->len;
        size_t total_bytes = sizeof(HeapHeader) + alloc_cap + 1;
        auto* mem = static_cast<char*>(run_malloc(total_bytes));
        auto* hdr = reinterpret_cast<HeapHeader*>(mem);
        hdr->len = static_cast<uint32_t>(len);
        hdr->alloc = static_cast<uint32_t>(alloc_cap);
        std::memcpy(mem + sizeof(HeapHeader), other.data(), len + 1);
        m_heap.header = hdr;
    }
}

SDS::SDS(SDS&& other) noexcept : m_flags(other.m_flags) {
    if (other.is_sso()) {
        m_sso = other.m_sso;
    } else {
        m_heap.header = other.m_heap.header;
    }
    other.m_flags = 0;
    other.m_sso.len = 0;
    other.m_sso.buf[0] = '\0';
}

SDS::~SDS() {
    if (!is_sso() && m_heap.header) {
        run_free(m_heap.header);
        m_heap.header = nullptr;
    }
}

SDS& SDS::operator=(const SDS& other) {
    if (this == &other) return *this;
    if (!is_sso() && m_heap.header) {
        run_free(m_heap.header);
    }
    m_flags = other.m_flags;
    if (is_sso()) {
        m_sso = other.m_sso;
    } else {
        size_t alloc_cap = other.m_heap.header->alloc;
        size_t len = other.m_heap.header->len;
        size_t total_bytes = sizeof(HeapHeader) + alloc_cap + 1;
        auto* mem = static_cast<char*>(run_malloc(total_bytes));
        auto* hdr = reinterpret_cast<HeapHeader*>(mem);
        hdr->len = static_cast<uint32_t>(len);
        hdr->alloc = static_cast<uint32_t>(alloc_cap);
        std::memcpy(mem + sizeof(HeapHeader), other.data(), len + 1);
        m_heap.header = hdr;
    }
    return *this;
}

SDS& SDS::operator=(SDS&& other) noexcept {
    if (this == &other) return *this;
    if (!is_sso() && m_heap.header) {
        run_free(m_heap.header);
    }
    m_flags = other.m_flags;
    if (other.is_sso()) {
        m_sso = other.m_sso;
    } else {
        m_heap.header = other.m_heap.header;
    }
    other.m_flags = 0;
    other.m_sso.len = 0;
    other.m_sso.buf[0] = '\0';
    return *this;
}

const char* SDS::data() const noexcept {
    if (is_sso()) {
        return m_sso.buf;
    }
    return reinterpret_cast<const char*>(m_heap.header) + sizeof(HeapHeader);
}

char* SDS::data() noexcept {
    if (is_sso()) {
        return m_sso.buf;
    }
    return reinterpret_cast<char*>(m_heap.header) + sizeof(HeapHeader);
}

size_t SDS::size() const noexcept {
    if (is_sso()) {
        return m_sso.len;
    }
    return m_heap.header ? m_heap.header->len : 0;
}

size_t SDS::capacity() const noexcept {
    if (is_sso()) {
        return SSO_CAPACITY;
    }
    return m_heap.header ? m_heap.header->alloc : 0;
}

void SDS::clear() noexcept {
    if (is_sso()) {
        m_sso.len = 0;
        m_sso.buf[0] = '\0';
    } else {
        if (m_heap.header) {
            m_heap.header->len = 0;
            char* payload = reinterpret_cast<char*>(m_heap.header) + sizeof(HeapHeader);
            payload[0] = '\0';
        }
    }
}

void SDS::reserve(size_t new_cap) {
    if (new_cap <= capacity()) return;
    if (new_cap <= SSO_CAPACITY && is_sso()) return;

    size_t current_len = size();
    size_t total_bytes = sizeof(HeapHeader) + new_cap + 1;

    if (is_sso()) {
        auto* mem = static_cast<char*>(run_malloc(total_bytes));
        auto* hdr = reinterpret_cast<HeapHeader*>(mem);
        hdr->len = static_cast<uint32_t>(current_len);
        hdr->alloc = static_cast<uint32_t>(new_cap);
        std::memcpy(mem + sizeof(HeapHeader), m_sso.buf, current_len + 1);
        m_flags = 1;
        m_heap.header = hdr;
    } else {
        auto* mem = static_cast<char*>(run_realloc(m_heap.header, total_bytes));
        auto* hdr = reinterpret_cast<HeapHeader*>(mem);
        hdr->alloc = static_cast<uint32_t>(new_cap);
        m_heap.header = hdr;
    }
}

void SDS::append(std::string_view sv) {
    append(sv.data(), sv.size());
}

void SDS::append(const char* s, size_t len) {
    if (len == 0) return;
    size_t current_len = size();
    size_t required = current_len + len;

    if (is_sso() && required <= SSO_CAPACITY) {
        std::memcpy(m_sso.buf + current_len, s, len);
        m_sso.len = static_cast<uint8_t>(required);
        m_sso.buf[required] = '\0';
        return;
    }

    if (required > capacity()) {
        // Amortized 1.5x growth
        size_t next_cap = (capacity() < 32) ? 64 : capacity() + (capacity() >> 1);
        if (next_cap < required) next_cap = required;
        reserve(next_cap);
    }

    char* target = data() + current_len;
    std::memcpy(target, s, len);
    target[len] = '\0';

    if (!is_sso()) {
        m_heap.header->len = static_cast<uint32_t>(required);
    }
}

void SDS::push_back(char ch) {
    append(&ch, 1);
}

bool SDS::operator==(const SDS& other) const noexcept {
    if (size() != other.size()) return false;
    return std::memcmp(data(), other.data(), size()) == 0;
}

bool SDS::operator==(std::string_view sv) const noexcept {
    if (size() != sv.size()) return false;
    return std::memcmp(data(), sv.data(), size()) == 0;
}

bool SDS::operator<(const SDS& other) const noexcept {
    return view() < other.view();
}

} // namespace rundb::core::internals
