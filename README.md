# RunDB Native (C++20)

<p align="center">
  <img src="https://raw.githubusercontent.com/DarshanAguru/runDB/main/RunDB.png" alt="RunDB Logo" width="220" />
</p>

<p align="center">
  <strong>An ultra-fast, memory-optimized, Redis-compatible in-memory key-value database engineered from scratch in modern C++20.</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-BSD%203--Clause-blue.svg" alt="License" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Language-C%2B%2B20-orange.svg" alt="C++20" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Throughput-150%2C000%2B%20QPS-brightgreen.svg" alt="Throughput" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Latency%20(p50)-0.15ms-blueviolet.svg" alt="Latency" /></a>
</p>

---

## Overview

**RunDB Native** is the high-performance native C++20 evolution of the [RunDB](https://github.com/DarshanAguru/runDB) distributed architecture. Designed for extreme speed, minimal memory overhead, and wire-level compatibility with standard Redis clients (`redis-cli`, Jedis, redis-py, etc.), RunDB Native implements proprietary algorithms and cache-conscious internal data structures rather than copying legacy architectures.

### Key Highlights

- **⚡ Blistering Performance**: Achieves **150,000+ QPS** on commodity hardware with **sub-0.2ms p50 latency**.
- **🧠 Custom `run_alloc` Engine**: Real-time atomic memory allocation tracking with zero lock contention, bundled directly with production `jemalloc` (`libjemalloc.so`) for zero-fragmentation operation.
- **📦 Bundled Zero-Config jemalloc**: Embedded into CMake with automatic RPATH resolution—no manual `apt-get install libjemalloc-dev` or environment tweaking required.
- **🧬 Proprietary `SDS`**: Small String Optimization (SSO) storing strings $\le 22$ bytes directly on the stack with **zero heap allocation**, paired with an 8-byte cache-line aligned dynamic header for longer keys.
- **🔢 Adaptive `IntSet`**: Compact contiguous sorted integer array automatically packing 16-bit, 32-bit, or 64-bit integers with branchless binary search, upgrading encodings in-place and auto-promoting to hash sets on demand.
- **📜 Chunked `QuickList`**: Cache-conscious unrolled doubly-linked list packing 32-element contiguous chunks per node, providing $O(1)$ push/pop at both ends with hardware cache prefetching.
- **⏰ 24-Bit Circular LRU Clock & Eviction Pool**: Approximates global LRU through a 16-candidate bounded eviction pool without pointer overhead.
- **🎯 Probabilistic Active Expiration**: Evaluates 20 random keys with TTL per database partition, automatically repeating if $> 25\%$ are expired, with a strict 1ms time-budget cap to maintain sub-millisecond p99 latencies.
- **💾 Append-Only File (AOF) Persistence**: Durability engine supporting `Always`, `EverySec` (1-second periodic background fsync), and `No` sync modes, with seamless startup replay.
- **🎨 Interactive Terminal Printer**: Full ANSI color palette, block-font ASCII art banner, boxed network status, dynamic metric scaling (B $\to$ KB $\to$ MB $\to$ GB), and an end-of-run "memory calories burnt" summary.

---

## Architecture & Directory Structure

```text
runDB_native/
├── include/
│   ├── core/
│   │   ├── internals/
│   │   │   ├── run_alloc.hpp         # Proprietary memory allocation & tracking engine
│   │   │   ├── sds.hpp               # Proprietary Small Dynamic String (SSO <= 22B)
│   │   │   ├── intset.hpp            # Adaptive contiguous integer set (16/32/64-bit)
│   │   │   ├── quicklist.hpp         # Chunked unrolled deque (32 items per chunk)
│   │   │   └── hashers.hpp           # 64-bit MurmurHash3 & FNV-1a hash functions
│   │   ├── client_context.hpp        # Active DB partition & transaction buffers
│   │   ├── object.hpp                # Bit-packed 32-bit object header (type, enc, LRU)
│   │   ├── database.hpp              # Partition keyspace with passive TTL checking
│   │   ├── evict_pool.hpp            # 16-candidate bounded LRU candidate pool
│   │   ├── eviction.hpp              # Memory threshold checks & eviction dispatch
│   │   ├── expiration.hpp            # Adaptive active key expiration daemon
│   │   ├── stats.hpp                 # Telemetry (hits, misses, commands, memory)
│   │   ├── aof.hpp                   # AOF persistence logger and startup replayer
│   │   ├── evaluator.hpp             # Command dispatch & evaluation engine
│   │   └── store.hpp                 # Multi-DB Store coordinator (SELECT 0..15)
│   ├── server/
│   │   ├── client.hpp                # Non-blocking socket client with sliding buffer
│   │   ├── shutdown.hpp              # Atomic engine states & POSIX signal handling
│   │   └── server.hpp                # Single-threaded Linux epoll event loop
│   ├── protocol/
│   │   ├── cmd.hpp                   # Command metadata
│   │   ├── resp_parser.hpp           # Zero-copy RESP processor & parser
│   │   └── resp.hpp                  # RESP serializer and encoder helpers
│   └── util/
│       └── printer.hpp               # Terminal ANSI printer & memory calories
├── src/                              # Corresponding C++20 implementations
├── config/
│   └── rundb.conf                    # Server configuration file
├── lib/
│   └── libjemalloc.so                # Bundled production jemalloc shared library
├── tests/                            # Native C++20 test and benchmarking suite
│   ├── test_framework.hpp            # Lightweight header-only test runner & assertions
│   ├── benchmark_main.cpp            # Native C++ benchmarking engine & network suite
│   └── test_*.cpp                    # Unit & integration tests for all subsystems
├── Dockerfile                        # Multi-stage production container build
├── docker-compose.yml                # Docker compose deployment
└── CMakeLists.txt                    # Modern CMake build system with presets
```

---

## Proprietary Internals & Algorithmic Design

### 1. `run_alloc` (Memory Allocation & Real-Time Tracking)
Standard `glibc malloc` exhibits memory fragmentation and lacks real-time insight into byte footprints. `run_alloc` implements:
- **Prefix Header (16-byte aligned)**: Every allocation is prefixed with a 16-byte metadata header `[size_t size][padding]`, ensuring maximum 16-byte alignment compatibility for AVX/SSE vector instructions.
- **Lock-Free Telemetry**: Tracks active bytes and block allocations via atomic counters with zero lock contention.
- **Embedded jemalloc**: Routes all allocations through bundled `jemalloc` with transparent RPATH resolution.

### 2. `SDS` (Small Dynamic String)
- **SSO ($\le 22$ bytes)**: Standard strings incur 32 bytes of struct overhead and heap allocations for keys like `"user:1"`. `SDS` stores strings up to 22 bytes in an inline 24-byte struct on the stack.
- **Dynamic Header ($> 22$ bytes)**: Prefixed with an 8-byte cache-aligned header `[uint32_t len, uint32_t alloc]` directly adjacent to character data, maximizing L1 cache line prefetching.
- **Binary Safe**: Length is stored explicitly, permitting null bytes `\0` in keys and values.

### 3. `IntSet` (Adaptive Compact Integer Set)
- **Multi-Width Encodings**: Evaluates values to store them in the smallest possible integer width:
  - `ENC_INT16` (2 bytes/val) for range $[-32768, 32767]$
  - `ENC_INT32` (4 bytes/val) for range $[-2147483648, 2147483647]$
  - `ENC_INT64` (8 bytes/val) for 64-bit numbers
- **In-Place Upgrading**: Upgrades existing elements backwards without auxiliary allocations when a wider integer is inserted. Auto-promotes to a hash set if string values are inserted or size exceeds 512.

### 4. `QuickList` (Chunked Unrolled Deque)
- **Cache-Conscious Chunking**: Pure linked lists cause cache thrashing due to pointer chasing. `QuickList` groups elements into contiguous arrays of 32 elements per `Node`.
- **$O(1)$ Boundaries**: LPUSH, RPUSH, LPOP, and RPOP operate at chunk boundaries in $O(1)$ without vector shifting.
- **Chunk Jumping**: Random indexing (`LINDEX`) skips full chunks of 32 ($k = \text{index} / 32$), minimizing pointer traversals.

### 5. Probabilistic Active Expiration
- Checks 20 random keys with TTL per database partition on each 50ms tick.
- If $> 25\%$ of sampled keys are expired, it resamples and purges immediately.
- Capped to 1ms total execution time per tick to avoid blocking network I/O.

---

---

## Performance Benchmarks & Comparison

Direct empirical comparison between **Python RunDB** and **Native C++ RunDB** using `redis-benchmark` (Linux 6.8 x86_64, 50 concurrent connections, 50,000 operations per command):

### Executive Summary

| Metric Category | Python RunDB | Native C++ RunDB | Relative Improvement / Multiplier |
|:---|:---:|:---:|:---:|
| **Average Throughput (RPS)** | **2,288.0 req/s** | **142,307.0 req/s** | **🚀 62.20x Faster** |
| **Average Median Latency (p50)** | **15.450 ms** | **0.162 ms** | **⚡ 99.0% Lower Latency** |
| **Average Tail Latency (p99)** | **19.636 ms** | **0.583 ms** | **⚡ 97.0% Lower Tail Latency** |
| **Peak Resident Memory (RSS)** | **313.07 MB** | **7.84 MB** | **💾 97.5% Less Memory** |
| **Memory Growth Delta** | **290.85 MB** | **2.36 MB** | **Zero fragmentation heap** |
| **Average CPU Core Load** | **44.3%** | **87.3%** | **High single-core saturation** |

### Command-by-Command Breakdown

| Command | Python RPS | Native C++ RPS | Throughput Gain | Python p50 | Native p50 | Native p99 |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| **`SET`** | 2,561 req/s | **137,362 req/s** | **53.6x faster** | 19.49 ms | **0.16 ms** | **0.69 ms** |
| **`GET`** | 3,104 req/s | **138,504 req/s** | **44.6x faster** | 16.75 ms | **0.16 ms** | **0.65 ms** |
| **`LPUSH`** | 2,784 req/s | **136,986 req/s** | **49.2x faster** | 18.50 ms | **0.17 ms** | **0.72 ms** |
| **`LPOP`** | 2,726 req/s | **134,770 req/s** | **49.4x faster** | 18.83 ms | **0.17 ms** | **0.56 ms** |
| **`SADD`** | 2,551 req/s | **144,927 req/s** | **56.8x faster** | 19.14 ms | **0.16 ms** | **0.54 ms** |
| **`PING`** | 0 req/s* | **161,290 req/s** | **Ultra-low overhead** | N/A* | **0.16 ms** | **0.35 ms** |

*\*Note: Python RunDB dropped connections under 50-client pipelined socket pressure during high-frequency PING.*

See [metrics.md](metrics.md) for full percentile distributions (min, avg, p50, p90, p95, p99, max).

---

## Quickstart

### Prerequisites
- Linux OS (Ubuntu 22.04+, Debian 12+, Arch, Fedora)
- GCC 13+ or Clang 17+ (with C++20 support)
- CMake 3.25+ and Ninja

### 1. Build from Source

```bash
# Clone the repository
git clone https://github.com/DarshanAguru/runDB_native.git
cd runDB_native

# Configure and compile in Release mode (-O3, LTO)
cmake --preset release
cmake --build --preset build-release
```

The resulting binary `./build/release/rundb` will be created with `libjemalloc.so` copied alongside it.

### 2. Run the Server

```bash
./build/release/rundb --port 7379
```

### 3. Connect with `redis-cli`

```bash
redis-cli -p 7379 PING
# Output: PONG

redis-cli -p 7379 SET user:name "Darshan"
redis-cli -p 7379 GET user:name
# Output: "Darshan"

redis-cli -p 7379 INFO memory
```

---

## Running with Docker & Docker Compose

RunDB Native includes a production multi-stage `Dockerfile`:

### Using Docker Compose
```bash
# Build and start in background
docker compose up -d

# Check logs
docker compose logs -f

# Stop container
docker compose down
```

### Using Docker CLI
```bash
docker build -t rundb-native:latest .
docker run -d -p 7379:7379 --name rundb-native -v rundb_data:/data rundb-native:latest
```

---

## Supported Commands

| Command | Category | Description |
|:---|:---|:---|
| `PING [msg]` | Admin | Ping the server; returns PONG or custom message |
| `ECHO msg` | Admin | Echo back the provided message |
| `SELECT index` | Admin | Switch active database partition (0..15) |
| `INFO [section]` | Admin | Server and human-readable memory analytics |
| `DBSIZE` | Admin | Count total keys in active partition |
| `TIME` | Admin | Current Unix server timestamp |
| `SET key val [EX sec] [PX ms] [NX|XX]` | Strings | Set key with optional TTL and conditions |
| `GET key` | Strings | Get value (with passive expiration) |
| `MSET k1 v1 [k2 v2 ...]` | Strings | Atomically set multiple key-value pairs |
| `MGET k1 [k2 ...]` | Strings | Retrieve multiple values |
| `INCR key` / `DECR key` | Strings | Increment or decrement 64-bit integer values |
| `INCRBY key delta` / `DECRBY key delta` | Strings | Add or subtract delta from integer values |
| `APPEND key val` | Strings | Append string to existing value |
| `STRLEN key` | Strings | Length of stored string |
| `LPUSH key val [val ...]` | Lists | Prepend values to list using chunked QuickList |
| `RPUSH key val [val ...]` | Lists | Append values to list |
| `LPOP key` / `RPOP key` | Lists | Remove and return elements from boundaries |
| `LLEN key` | Lists | Length of list |
| `LINDEX key index` | Lists | Element at index (supports negative offsets) |
| `LRANGE key start stop` | Lists | Slice of list elements |
| `SADD key member [member ...]` | Sets | Add members (adaptive IntSet $\to$ HashSet) |
| `SREM key member [member ...]` | Sets | Remove members |
| `SISMEMBER key member` | Sets | Test set membership |
| `SCARD key` | Sets | Cardinality of set |
| `SMEMBERS key` | Sets | Return all members of set |
| `SRANDMEMBER key` | Sets | Return random member from set |
| `DEL key [key ...]` | Generic | Delete keys |
| `EXISTS key [key ...]` | Generic | Check key existence |
| `TYPE key` | Generic | Return object type (`string`, `list`, `set`) |
| `EXPIRE key sec` / `PEXPIRE key ms` | Expiry | Set expiration timeout |
| `TTL key` / `PTTL key` | Expiry | Check remaining time-to-live |
| `PERSIST key` | Expiry | Remove expiration timeout |
| `FLUSHDB` | Generic | Flush active database partition |
| `FLUSHALL` | Generic | Flush all 16 database partitions |
| `MULTI` | Transactions | Begin transaction block |
| `EXEC` | Transactions | Execute all queued commands atomically |
| `DISCARD` | Transactions | Cancel transaction block |

---

## Configuration (`rundb.conf`) & Precedence

RunDB Native implements strict 4-tier configuration precedence:
1. **CLI Flags** (e.g. `--port 7400`, `--memory-limit 104857600`) — Highest Priority
2. **Environment Variables** (e.g. `RUNDB_PORT=7400`, `RUNDB_LOG_LEVEL=DEBUG`)
3. **Configuration File** (`config/rundb.conf` or `--config <path>`)
4. **Built-in Production Defaults** (Port 7379, 0.0.0.0, 100MB limit, allkeys-lru) — Lowest Priority

> [!NOTE]
> RunDB cleanly ingests environment variables at startup without mutating or polluting the host environment (zero `setenv` side-effects).

Example `rundb.conf`:

```conf
# Network Binding
host 0.0.0.0
port 7379
log_level INFO

# Memory & Eviction (100MB limit)
maxmemory 104857600
maxmemory_policy allkeys-lru

# AOF Durability
aof_enabled yes
aof_file appendonly.aof
aof_fsync everysec
```

Start the server pointing to a configuration file:
```bash
./build/release/rundb --config config/rundb.conf
```

Or override individual settings on the command line:
```bash
./build/release/rundb --config config/rundb.conf --port 8000 --log-level DEBUG
```

---

## Testing & Benchmarking

RunDB Native features a comprehensive native C++20 testing framework and high-performance benchmarking suite.

### 1. Running the Automated Test Suite

Run the full test suite directly or through CTest:
```bash
# Direct test binary (runs 57 unit and integration tests)
./build/rundb_tests

# Filter tests by subsystem or test name
./build/rundb_tests --filter Evaluator

# Run via CMake CTest
ctest --test-dir build --output-on-failure
```

### 2. Running Native C++ Benchmarks

Run high-resolution benchmarks measuring RPS, latency percentiles (min, avg, p50, p90, p95, p99, max), and memory RSS deltas without Python interpreter overhead:
```bash
# Run direct in-memory engine microbenchmarks (millions of ops/sec)
./build/release/rundb_benchmark --engine -n 100000

# Run high-concurrency TCP network benchmark
./build/release/rundb_benchmark --network -c 50 -n 50000

# Run all benchmarks
./build/release/rundb_benchmark
```

---

## Contributing & License

Contributions are welcome! Please review [CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) before opening a pull request.

Protected under the [BSD 3-Clause](LICENSE) License.

**Author**: **Darshan Aguru**
- 📧 Email: agurudf@gmail.com
- 🌐 Website: [thisdarshiii.in](https://thisdarshiii.in)
- 🐙 GitHub: [@DarshanAguru](https://github.com/DarshanAguru)
