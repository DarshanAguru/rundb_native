#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include <utility>

namespace rundb::testing {

// Terminal ANSI colors
namespace color {
    inline constexpr const char* RESET   = "\033[0m";
    inline constexpr const char* BOLD    = "\033[1m";
    inline constexpr const char* RED     = "\033[31m";
    inline constexpr const char* GREEN   = "\033[32m";
    inline constexpr const char* YELLOW  = "\033[33m";
    inline constexpr const char* BLUE    = "\033[34m";
    inline constexpr const char* CYAN    = "\033[36m";
    inline constexpr const char* WHITE   = "\033[37m";
}

class TestFailureException : public std::runtime_error {
public:
    TestFailureException(const std::string& msg, const char* file, int line)
        : std::runtime_error(msg), file_(file), line_(line) {}

    [[nodiscard]] const char* file() const noexcept { return file_; }
    [[nodiscard]] int line() const noexcept { return line_; }

private:
    const char* file_;
    int line_;
};

struct TestCase {
    std::string name;
    std::function<void()> func;
    const char* file;
    int line;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void register_test(std::string name, std::function<void()> func, const char* file, int line) {
        tests_.push_back({std::move(name), std::move(func), file, line});
    }

    [[nodiscard]] const std::vector<TestCase>& tests() const { return tests_; }

    int run_all(int argc = 0, char* argv[] = nullptr) {
        std::string filter;
        bool list_only = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-f" || arg == "--filter") && i + 1 < argc) {
                filter = argv[++i];
            } else if (arg == "-l" || arg == "--list") {
                list_only = true;
            } else if (arg == "-h" || arg == "--help") {
                std::cout << "RunDB Native Test Runner\n"
                          << "Usage: rundb_tests [OPTIONS]\n"
                          << "  -f, --filter <str>   Only run tests containing <str>\n"
                          << "  -l, --list           List registered tests without running\n"
                          << "  -h, --help           Show this help message\n";
                return 0;
            }
        }

        if (list_only) {
            std::cout << color::BOLD << "Registered Tests (" << tests_.size() << " total):\n" << color::RESET;
            for (const auto& t : tests_) {
                std::cout << "  - " << t.name << " (" << t.file << ":" << t.line << ")\n";
            }
            return 0;
        }

        std::cout << color::BOLD << color::CYAN
                  << "========================================================\n"
                  << "            RunDB Native C++ Test Suite                 \n"
                  << "========================================================\n"
                  << color::RESET << "\n";

        size_t passed = 0;
        size_t failed = 0;
        size_t skipped = 0;

        auto total_start = std::chrono::high_resolution_clock::now();

        for (const auto& test : tests_) {
            if (!filter.empty() && test.name.find(filter) == std::string::npos) {
                skipped++;
                continue;
            }

            std::cout << color::CYAN << "[ RUN      ] " << color::RESET << color::BOLD << test.name << color::RESET << std::flush;

            auto t0 = std::chrono::high_resolution_clock::now();
            bool test_ok = false;
            std::string error_msg;
            std::string err_loc;

            try {
                test.func();
                test_ok = true;
            } catch (const TestFailureException& ex) {
                error_msg = ex.what();
                err_loc = std::string(ex.file()) + ":" + std::to_string(ex.line());
            } catch (const std::exception& ex) {
                error_msg = std::string("Unexpected standard exception: ") + ex.what();
            } catch (...) {
                error_msg = "Unknown non-standard exception thrown";
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

            if (test_ok) {
                passed++;
                std::cout << "\r" << color::GREEN << "[       OK ] " << color::RESET
                          << test.name << " (" << duration_us << " µs)\n";
            } else {
                failed++;
                std::cout << "\r" << color::RED << "[  FAILED  ] " << color::RESET
                          << color::BOLD << test.name << color::RESET
                          << " (" << duration_us << " µs)\n";
                if (!err_loc.empty()) {
                    std::cout << "             " << color::YELLOW << "Location: " << color::RESET << err_loc << "\n";
                }
                std::cout << "             " << color::RED << "Failure: " << color::RESET << error_msg << "\n";
            }
        }

        auto total_end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

        std::cout << "\n" << color::BOLD << color::CYAN
                  << "========================================================\n"
                  << "                      Test Results                      \n"
                  << "========================================================\n"
                  << color::RESET;

        std::cout << "  " << color::BOLD << "Total Tests: " << color::RESET << (passed + failed) << "\n";
        std::cout << "  " << color::GREEN << color::BOLD << "Passed:      " << color::RESET << color::GREEN << passed << color::RESET << "\n";
        if (failed > 0) {
            std::cout << "  " << color::RED << color::BOLD << "Failed:      " << color::RESET << color::RED << failed << color::RESET << "\n";
        }
        if (skipped > 0) {
            std::cout << "  " << color::YELLOW << "Skipped:     " << color::RESET << skipped << "\n";
        }
        std::cout << "  " << color::BOLD << "Duration:    " << color::RESET << std::fixed << std::setprecision(2) << total_ms << " ms\n";
        std::cout << color::BOLD << color::CYAN
                  << "========================================================\n"
                  << color::RESET;

        if (failed == 0) {
            std::cout << color::GREEN << color::BOLD << "🎉 ALL TESTS PASSED SUCCESSFULLY!" << color::RESET << "\n\n";
            return 0;
        } else {
            std::cout << color::RED << color::BOLD << "❌ " << failed << " TEST(S) FAILED!" << color::RESET << "\n\n";
            return 1;
        }
    }

private:
    std::vector<TestCase> tests_;
};

struct TestRegistrar {
    TestRegistrar(std::string name, std::function<void()> func, const char* file, int line) {
        TestRegistry::instance().register_test(std::move(name), std::move(func), file, line);
    }
};

template <typename T1, typename T2>
bool test_eq(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_equal(a, b);
    } else {
        return a == b;
    }
}

template <typename T1, typename T2>
bool test_ne(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_not_equal(a, b);
    } else {
        return a != b;
    }
}

template <typename T1, typename T2>
bool test_lt(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_less(a, b);
    } else {
        return a < b;
    }
}

template <typename T1, typename T2>
bool test_le(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_less_equal(a, b);
    } else {
        return a <= b;
    }
}

template <typename T1, typename T2>
bool test_gt(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_greater(a, b);
    } else {
        return a > b;
    }
}

template <typename T1, typename T2>
bool test_ge(const T1& a, const T2& b) {
    if constexpr (std::is_integral_v<T1> && std::is_integral_v<T2>) {
        return std::cmp_greater_equal(a, b);
    } else {
        return a >= b;
    }
}

inline void assert_contains_func(std::string_view haystack, std::string_view needle, const char* file, int line) {
    if (haystack.find(needle) == std::string_view::npos) {
        std::ostringstream oss;
        oss << "ASSERT_CONTAINS failed: expected '" << needle << "' in '" << haystack << "'";
        throw ::rundb::testing::TestFailureException(oss.str(), file, line);
    }
}

} // namespace rundb::testing

// Concatenation helper macros
#define RUNDB_CONCAT_INNER(a, b) a##b
#define RUNDB_CONCAT(a, b) RUNDB_CONCAT_INNER(a, b)

#define TEST_CASE(name) \
    static void RUNDB_CONCAT(rundb_test_func_, __LINE__)(); \
    static const ::rundb::testing::TestRegistrar RUNDB_CONCAT(rundb_test_reg_, __LINE__)( \
        name, RUNDB_CONCAT(rundb_test_func_, __LINE__), __FILE__, __LINE__); \
    static void RUNDB_CONCAT(rundb_test_func_, __LINE__)()

// Assertion macros
#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_TRUE failed: (" #cond ") evaluated to false"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if ((cond)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_FALSE failed: (" #cond ") evaluated to true"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_eq(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_EQ failed: " #val1 " == " #val2 " (" << _v1 << " != " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_NE(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_ne(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NE failed: " #val1 " != " #val2 " (" << _v1 << " == " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_LT(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_lt(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_LT failed: " #val1 " < " #val2 " (" << _v1 << " >= " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_LE(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_le(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_LE failed: " #val1 " <= " #val2 " (" << _v1 << " > " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_GT(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_gt(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_GT failed: " #val1 " > " #val2 " (" << _v1 << " <= " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_GE(val1, val2) \
    do { \
        auto _v1 = (val1); \
        auto _v2 = (val2); \
        if (!::rundb::testing::test_ge(_v1, _v2)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_GE failed: " #val1 " >= " #val2 " (" << _v1 << " < " << _v2 << ")"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool _threw = false; \
        try { \
            (void)(expr); \
        } catch (const ExceptionType&) { \
            _threw = true; \
        } catch (...) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_THROWS failed: " #expr " threw a different exception type than " #ExceptionType; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
        if (!_threw) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_THROWS failed: " #expr " did not throw " #ExceptionType; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_NO_THROW(expr) \
    do { \
        try { \
            (void)(expr); \
        } catch (const std::exception& _e) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NO_THROW failed: " #expr " threw: " << _e.what(); \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } catch (...) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NO_THROW failed: " #expr " threw unknown exception"; \
            throw ::rundb::testing::TestFailureException(_oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_CONTAINS(h, n) ::rundb::testing::assert_contains_func((h), (n), __FILE__, __LINE__)
