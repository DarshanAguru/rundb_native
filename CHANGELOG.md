# Changelog

All notable changes to **RunDB** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2026-09-29

### Added
- **Native C++20 Architecture**: Ground-up rewrite of RunDB in modern C++20, removing Python runtime overhead, GIL lock contention, and `ctypes` foreign-function boundaries.
- **Empirical 62x Performance Leap**: Validated through rigorous `redis-benchmark` tests:
  - Throughput: **142,307 RPS** average (vs 2,288 RPS in Python) — **62.2x throughput increase**.
  - Latency: **0.162 ms p50** (vs 15.450 ms in Python) — **99.0% latency reduction**.
  - Tail Latency: **0.583 ms p99** (vs 19.636 ms in Python) — **97.0% tail latency reduction**.
  - Memory Footprint: **7.84 MB peak RSS** (vs 313.07 MB in Python) — **97.5% memory reduction**.
- **Proprietary Memory Allocator (`run_alloc`)**: 16-byte aligned header prefix metadata `[size_t size][padding]` for AVX/SSE vector instructions, real-time atomic byte and block telemetry, and bundled production `jemalloc` (`lib/libjemalloc.so`) embedded via CMake with automatic RPATH.
- **Proprietary Small Dynamic String (`SDS`)**: Zero-heap Small String Optimization (SSO) storing strings $\le 22$ bytes inline on the stack, paired with an 8-byte cache-aligned dynamic header for longer strings.
- **Adaptive Contiguous Integer Set (`IntSet`)**: In-place packed 16/32/64-bit array with branchless binary search, automatic backwards upgrading, and auto-promotion to hash set on non-integer insertion.
- **Chunked Deque (`QuickList`)**: Cache-conscious unrolled doubly linked list with 32-element contiguous node buffers and hardware cache line prefetching.
- **Production Epoll Network Engine**: Single-threaded non-blocking Linux epoll reactor with sliding circular read buffer and direct zero-context-switch write fast-path.
- **AOF Durability Engine**: Full RESP wire-format logging, atomic snapshotting (`BGREWRITEAOF`) via `.tmp` swap, configurable fsync (`always`, `everysec`, `no`), and automatic startup replay.
- **Interactive Terminal UI**: ANSI color palette, block-font ASCII art banner, boxed network status, dynamic metric auto-scaling (Bytes, KB, MB, GB, %), and "memory calories burnt" shutdown calculation.

### Changed
- **Zero-Pollution Environment Handling**: Eliminated all `setenv()` process mutations from configuration parsing. Environment variables (`RUNDB_HOST`, `RUNDB_PORT`, `RUNDB_LOG_LEVEL`, etc.) are ingested cleanly with strict precedence (CLI Flags > Environment Variables > `config/rundb.conf` > Built-in Defaults).
- **Decoupled Asynchronous Logging**: `rundb::Logger::init(opts.log_level)` now accepts explicit log level arguments with lock-free Quill background worker thread.

## [1.1.0] - 2026-06-13

### Added
- **C-Heap Open-Addressing HashMap**: Implemented a native, open-addressing Hash Map on the C heap utilizing FNV-1a hashing and tombstoning, completely eliminating Python `dict` overhead from core database storage.
- **Intset and HashTable Upgradeable Sets**: Implemented Redis-style Sets that start as contiguous integer-sorted arrays (`Intset`) and automatically upgrade to C-heap Hash Tables (`HashTable`) when non-integer values are inserted or size thresholds are exceeded.
- **Redis Set Commands Support**: Added command handlers and evaluator dispatching for `SADD`, `SISMEMBER`, `SCARD`, `SMEMBERS`, `SRANDMEMBER`, and `SREM`.
- **QuickList and ZipList Data Structures**: Implemented memory-efficient list types using doubly-linked structures (`QuickList`) of packed contiguous memory buffers (`ZipList`).
- **Redis List Commands Support**: Added command handlers and evaluator dispatching for `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LINDEX`, and `LRANGE`.
- **`DEBUG OBJECT` Command**: Added a diagnostic tool command `DEBUG OBJECT <key>` to retrieve object pointers, encoding types, serialized lengths, and LRU idle times.
- **Double-Free Safe Ownership Handoff**: Added pointer finalizer detaching via a new `release()` mechanism in `QuickList`, `Set`, and `RedisObject` to safely transfer structure ownership from Python to `RedisObject` without double-free errors.
- **Comprehensive Set & List Test Suites**: Added new automated unit tests in `tests/test_set.py` and `tests/test_list_commands.py` validating command lifecycles, encodings, and memory recycling.

## [1.0.0] - 2026-06-11

### Added
- **Pre-run Prerequisite Check**: Added a system compatibility verification in `main.py` checking for Linux OS, `select.epoll` support, and `ctypes` C-library binding before booting the server.
- **Eviction and Transaction Storm Utilities**: Added new test helper scripts `eviction_storm.py` and `transaction_storm.py` in `testing_utils/` to load-test key evictions and concurrent transactions.
- **Native Memory Management (`zmalloc`)**: Implemented a custom `ctypes` wrapper for C-level memory allocation, enabling real-time memory tracking and integration with `jemalloc` for optimized memory layout.
- **Approximated LRU Eviction**: Added Approximated Least Recently Used eviction strategy using a dynamic sorted eviction candidate pool, alongside `simple-first` and `allkeys-random` strategies.
- **Redis Transactions**: Full transaction support with `MULTI`, `EXEC`, and `DISCARD` isolating transaction states/queues per client.
- **Graceful Shutdown**: Traps OS termination signals to cleanly exit the event loop, serialize all in-memory keyspaces to AOF, and shut down gracefully.
- **Large Request Handling**: Added read/write buffers in `FDComm` to handle MTU chunking and partial/large RESP command streaming.
- **Active & Passive Expiration**: Dual-strategy expiration cleaning (passive on-access deletion + periodic active sampling cron).
- **AOF Snapshotting & Forking**: Non-blocking database snapshot dumps using background process forking, with full TTL/expiration persistence for active keys.


### Changed
- **Directory Restructuring**: Renamed the `utils/` directory to `testing_utils/` and updated documentation references.
- **Server Utility Modularization**: Reorganized the server architecture, placing `Printer.py` and `Shutdown.py` under the `server/util/` package and updating imports.
- **Architecture Diagram**: Generated and updated the visual architecture schema (`RunDB.png`) with clean, modernized nodes mapping all current components.
