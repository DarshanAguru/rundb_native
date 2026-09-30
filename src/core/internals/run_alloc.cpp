#include "core/internals/run_alloc.hpp"
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <fstream>
#include <unistd.h>

#ifdef RUNDB_USE_JEMALLOC
extern "C" {
    int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
}
#endif

namespace rundb::core::internals::run_alloc {

// 16-byte aligned prefix to preserve max_align_t alignment for SIMD/AVX
struct alignas(16) AllocHeader {
    size_t size;
    size_t padding; // Pad to 16 bytes
};

static constexpr size_t PREFIX_SIZE = sizeof(AllocHeader);

static std::atomic<size_t> s_allocated_bytes{0};
static std::atomic<size_t> s_allocated_blocks{0};

void* run_malloc(size_t size) {
    size_t total = PREFIX_SIZE + size;
    auto* raw = static_cast<char*>(std::malloc(total));
    if (!raw) throw std::bad_alloc();

    auto* hdr = reinterpret_cast<AllocHeader*>(raw);
    hdr->size = size;
    hdr->padding = 0;

    s_allocated_bytes.fetch_add(size, std::memory_order_relaxed);
    s_allocated_blocks.fetch_add(1, std::memory_order_relaxed);

    return raw + PREFIX_SIZE;
}

void* run_calloc(size_t num, size_t size) {
    size_t payload_bytes = num * size;
    size_t total = PREFIX_SIZE + payload_bytes;

    auto* raw = static_cast<char*>(std::calloc(1, total));
    if (!raw) throw std::bad_alloc();

    auto* hdr = reinterpret_cast<AllocHeader*>(raw);
    hdr->size = payload_bytes;
    hdr->padding = 0;

    s_allocated_bytes.fetch_add(payload_bytes, std::memory_order_relaxed);
    s_allocated_blocks.fetch_add(1, std::memory_order_relaxed);

    return raw + PREFIX_SIZE;
}

void* run_realloc(void* ptr, size_t new_size) {
    if (!ptr) return run_malloc(new_size);
    if (new_size == 0) {
        run_free(ptr);
        return nullptr;
    }

    auto* raw = static_cast<char*>(ptr) - PREFIX_SIZE;
    auto* hdr = reinterpret_cast<AllocHeader*>(raw);
    size_t old_size = hdr->size;

    size_t total = PREFIX_SIZE + new_size;
    auto* new_raw = static_cast<char*>(std::realloc(raw, total));
    if (!new_raw) throw std::bad_alloc();

    auto* new_hdr = reinterpret_cast<AllocHeader*>(new_raw);
    new_hdr->size = new_size;

    if (new_size >= old_size) {
        s_allocated_bytes.fetch_add(new_size - old_size, std::memory_order_relaxed);
    } else {
        s_allocated_bytes.fetch_sub(old_size - new_size, std::memory_order_relaxed);
    }

    return new_raw + PREFIX_SIZE;
}

void run_free(void* ptr) noexcept {
    if (!ptr) return;

    auto* raw = static_cast<char*>(ptr) - PREFIX_SIZE;
    auto* hdr = reinterpret_cast<AllocHeader*>(raw);
    size_t size = hdr->size;

    s_allocated_bytes.fetch_sub(size, std::memory_order_relaxed);
    s_allocated_blocks.fetch_sub(1, std::memory_order_relaxed);

    std::free(raw);
}

size_t run_usable_size(const void* ptr) noexcept {
    if (!ptr) return 0;
    const auto* raw = static_cast<const char*>(ptr) - PREFIX_SIZE;
    const auto* hdr = reinterpret_cast<const AllocHeader*>(raw);
    return hdr->size;
}

size_t get_used_memory() noexcept {
    return s_allocated_bytes.load(std::memory_order_relaxed);
}

size_t get_used_blocks() noexcept {
    return s_allocated_blocks.load(std::memory_order_relaxed);
}

size_t get_resident_memory() noexcept {
#ifdef RUNDB_USE_JEMALLOC
    size_t resident = 0;
    size_t sz = sizeof(resident);
    if (mallctl("stats.resident", &resident, &sz, nullptr, 0) == 0 && resident > 0) {
        return resident;
    }
#endif
    // Fallback: read RSS from /proc/self/statm
    long page_size = ::sysconf(_SC_PAGESIZE);
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t total_pages = 0, rss_pages = 0;
        if (statm >> total_pages >> rss_pages) {
            return rss_pages * static_cast<size_t>(page_size);
        }
    }
    return get_used_memory();
}

std::string get_allocator_name() noexcept {
#ifdef RUNDB_USE_JEMALLOC
    return "jemalloc";
#else
    return "libc";
#endif
}

} // namespace rundb::core::internals::run_alloc
