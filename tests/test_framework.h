#pragma once

#include <iostream>
#include <string>
#include <atomic>
#include <chrono>

namespace flowcoro::test {

// 
class TestRunner {
private:
    static std::atomic<int> passed_;
    static std::atomic<int> failed_;

public:
    static void expect_true(bool condition, const std::string& message,
                           const char* file, int line) {
        if (condition) {
            passed_++;
            std::cout << " PASS: " << message << std::endl;
        } else {
            failed_++;
            std::cerr << " FAIL: " << message
                     << " (" << file << ":" << line << ")" << std::endl;
        }
    }

    static void expect_eq(const auto& a, const auto& b, const std::string& message,
                         const char* file, int line) {
        // static_cast
        if constexpr (std::is_integral_v<std::decay_t<decltype(a)>> &&
                      std::is_integral_v<std::decay_t<decltype(b)>>) {
            if (static_cast<std::common_type_t<decltype(a), decltype(b)>>(a) ==
                static_cast<std::common_type_t<decltype(a), decltype(b)>>(b)) {
                passed_++;
                std::cout << " PASS: " << message << std::endl;
            } else {
                failed_++;
                std::cerr << " FAIL: " << message << " - Expected: " << b
                         << ", Got: " << a << " (" << file << ":" << line << ")" << std::endl;
            }
        } else {
            if (a == b) {
                passed_++;
                std::cout << " PASS: " << message << std::endl;
            } else {
                failed_++;
                std::cerr << " FAIL: " << message << " - Expected: " << b
                         << ", Got: " << a << " (" << file << ":" << line << ")" << std::endl;
            }
        }
    }

    static void print_summary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Passed: " << passed_.load() << std::endl;
        std::cout << "Failed: " << failed_.load() << std::endl;
        std::cout << "Total: " << (passed_.load() + failed_.load()) << std::endl;

        if (failed_.load() == 0) {
            std::cout << " All tests passed!" << std::endl;
        } else {
            std::cout << " " << failed_.load() << " test(s) failed!" << std::endl;
        }
    }

    static bool all_passed() {
        return failed_.load() == 0;
    }

    static void reset() {
        passed_ = 0;
        failed_ = 0;
    }
};

// 
std::atomic<int> TestRunner::passed_{0};
std::atomic<int> TestRunner::failed_{0};

// 
#define TEST_EXPECT_TRUE(condition) \
    flowcoro::test::TestRunner::expect_true((condition), #condition, __FILE__, __LINE__)

#define TEST_EXPECT_FALSE(condition) \
    flowcoro::test::TestRunner::expect_true(!(condition), "!(" #condition ")", __FILE__, __LINE__)

#define TEST_EXPECT_EQ(a, b) \
    flowcoro::test::TestRunner::expect_eq((a), (b), #a " == " #b, __FILE__, __LINE__)

#define TEST_EXPECT_NE(a, b) \
    flowcoro::test::TestRunner::expect_true((a) != (b), #a " != " #b, __FILE__, __LINE__)

// 
#define TEST_CASE(name) \
    void test_##name(); \
    namespace { \
        struct name##_runner { \
            name##_runner() { \
                std::cout << "\n--- Running: " #name " ---" << std::endl; \
                test_##name(); \
            } \
        }; \
        static name##_runner name##_instance; \
    } \
    void test_##name()

// 
class TestSuite {
public:
    TestSuite(const std::string& suite_name) : name_(suite_name) {
        std::cout << "\n Test Suite: " << name_ << std::endl;
        std::cout << std::string(40, '=') << std::endl;
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    ~TestSuite() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time_);

        std::cout << std::string(40, '=') << std::endl;
        std::cout << "Suite '" << name_ << "' completed in "
                 << duration.count() << "ms" << std::endl;
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

#define TEST_SUITE(name) flowcoro::test::TestSuite suite(name)

} // namespace flowcoro::test
