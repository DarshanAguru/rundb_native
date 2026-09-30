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
* If you've added code that should be tested, add or update appropriate scripts in the `testing_utils/` directory.
* Ensure your code adheres to Python standards (PEP 8).
* Update documentation (`README.md`, etc.) if your change introduces new configuration settings, commands, or behaviors.
* Write clear, descriptive commit messages.

---

## Development Setup

1. **Clone your fork**:
   ```bash
   git clone https://github.com/DarshanAguru/rundb_native.git
   cd runDB
   ```

2. **Set up a virtual environment**:
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   ```

3. **Run the server locally**:
   - Default:
     ```bash
     python3 main.py
     ```
   - Using the `jemalloc` memory allocator:
     ```bash
     LD_PRELOAD=./dll/libjemalloc.so python3 main.py
     ```

4. **Running tests**:
   Run the automated unit test suite:
   ```bash
   python3 tests/run_tests.py
   ```

   You can also run utility scripts to storm/benchmark the database:
   ```bash
   python3 testing_utils/set_storm.py
   python3 testing_utils/set_storm_with_expiration.py
   python3 testing_utils/eviction_storm.py
   python3 testing_utils/transaction_storm.py
   ```
