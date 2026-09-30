#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <new>

namespace rundb::core::internals::run_alloc {

/**
 * @brief RunAlloc: Proprietary Memory Allocation and Real-Time Tracking Engine.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Prefix Header (16-byte aligned):
 *    - Prepends a size metadata header to every allocation, aligned to max_align_t.
 *    - Guarantees AVX/SSE alignment safety while enabling exact O(1) size retrieval.
 *
 * 2. High-Performance Real-Time Tracking:
 *    - Maintains lock-free atomic counters for active byte footprint and block count.
 *    - Zero lock contention between worker threads and background maintenance.
 *
 * 3. Native jemalloc Integration:
 *    - Integrates with bundled jemalloc to minimize heap fragmentation under high churn.
 *    - Introspects physical resident memory (RSS) via jemalloc mallctl.
 */

// Memory allocation primitives
void* run_malloc(size_t size);
void* run_calloc(size_t num, size_t size);
void* run_realloc(void* ptr, size_t new_size);
void  run_free(void* ptr) noexcept;

// Introspection & telemetry
size_t run_usable_size(const void* ptr) noexcept;
size_t get_used_memory() noexcept;
size_t get_used_blocks() noexcept;
size_t get_resident_memory() noexcept;
std::string get_allocator_name() noexcept;

} // namespace rundb::core::internals::run_alloc
