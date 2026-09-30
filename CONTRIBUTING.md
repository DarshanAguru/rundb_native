# Contributing to RunDB

First off, thank you for considering contributing to **RunDB**! It is people like you who make learning-focused projects so valuable for the community.

Please read through the guidelines below before making or submitting changes.

---

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md). Please report any unacceptable behavior to the project maintainers.

## How Can I Contribute?

### 1. Reporting Bugs
* Check the existing issues/PRs to see if the bug has already been reported.
* If not, open a new issue containing:
  * A clear, descriptive title.
  * Steps to reproduce the bug.
  * Your operating system (remember, `RunDB` is Linux-only due to `select.epoll`).
  * Expected vs. actual behavior, along with relevant server logs.

### 2. Suggesting Enhancements
* Open an issue explaining the feature/enhancement you'd like to suggest.
* Explain why this enhancement would be useful (e.g., adding support for a new Redis command or implementing a new eviction strategy).

### 3. Submitting Pull Requests
* **Fork** the repository and create your branch from `main`.
* If you've added code that should be tested, add or update appropriate tests in the `tests/` directory.
* Ensure your code adheres to modern C++20 conventions.
* Update documentation (`README.md`, etc.) if your change introduces new configuration settings, commands, or behaviors.
* Write clear, descriptive commit messages.

---

## Development Setup

1. **Clone your fork**:
   ```bash
   git clone https://github.com/DarshanAguru/rundb_native.git
   cd rundb_native
   ```

2. **Build the project**:
   ```bash
   # Debug build (with assertions and sanitizer support)
   cmake --preset default
   cmake --build --preset build-debug

   # Release build (maximum optimization -O3 -march=native)
   cmake --preset release
   cmake --build --preset build-release
   ```

3. **Run the server locally**:
   ```bash
   ./build/release/rundb --port 7379
   ```

4. **Running tests**:
   Run the native C++ unit & integration test suite:
   ```bash
   ./build/rundb_tests

   # Or via CTest
   ctest --test-dir build --output-on-failure
   ```

5. **Running benchmarks**:
   Run the native C++ benchmarking suite:
   ```bash
   # In-memory engine microbenchmarks
   ./build/release/rundb_benchmark --engine

   # Network TCP benchmark
   ./build/release/rundb_benchmark --network
   ```
