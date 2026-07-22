/**
 * @file test_timeout.cpp
 * @brief Tests for with_timeout composable timeout.
 */

#include <chrono>
#include <iostream>
#include <string>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

TEST_CASE(with_timeout_success) {
    std::cout << "\n=== with_timeout success ===\n";
    auto runner = []() -> Task<void> {
        auto quick = []() -> Task<int> {
            co_await ClockAwaiter(milliseconds(10));
            co_return 42;
        };

        int v = -1;
        bool threw = false;
        try {
            v = co_await with_timeout(quick(), milliseconds(200));
        } catch (const TimeoutException&) {
            threw = true;
        }
        TEST_EXPECT_FALSE(threw);
        TEST_EXPECT_EQ(v, 42);
    }();
    sync_wait(std::move(runner));
}

TEST_CASE(with_timeout_fires) {
    std::cout << "\n=== with_timeout fires ===\n";
    auto runner = []() -> Task<void> {
        auto slow = []() -> Task<int> {
            co_await ClockAwaiter(milliseconds(500));
            co_return 99;
        };

        bool threw = false;
        try {
            co_await with_timeout(slow(), milliseconds(20));
        } catch (const TimeoutException&) {
            threw = true;
        }
        TEST_EXPECT_TRUE(threw);
    }();
    sync_wait(std::move(runner));
}

TEST_CASE(with_timeout_void_success) {
    std::cout << "\n=== with_timeout<void> success ===\n";
    auto runner = []() -> Task<void> {
        auto quick = []() -> Task<void> {
            co_await ClockAwaiter(milliseconds(5));
            co_return;
        };
        bool threw = false;
        try {
            co_await with_timeout(quick(), milliseconds(200));
        } catch (const TimeoutException&) {
            threw = true;
        }
        TEST_EXPECT_FALSE(threw);
    }();
    sync_wait(std::move(runner));
}

TEST_CASE(with_timeout_void_fires) {
    std::cout << "\n=== with_timeout<void> fires ===\n";
    auto runner = []() -> Task<void> {
        auto slow = []() -> Task<void> {
            co_await ClockAwaiter(milliseconds(500));
            co_return;
        };
        bool threw = false;
        try {
            co_await with_timeout(slow(), milliseconds(20));
        } catch (const TimeoutException&) {
            threw = true;
        }
        TEST_EXPECT_TRUE(threw);
    }();
    sync_wait(std::move(runner));
}

TEST_CASE(with_timeout_string_value) {
    std::cout << "\n=== with_timeout string value ===\n";
    auto runner = []() -> Task<void> {
        auto quick = []() -> Task<std::string> {
            co_await ClockAwaiter(milliseconds(10));
            co_return std::string("ok");
        };
        std::string v;
        bool threw = false;
        try {
            v = co_await with_timeout(quick(), milliseconds(200));
        } catch (const TimeoutException&) {
            threw = true;
        }
        TEST_EXPECT_FALSE(threw);
        TEST_EXPECT_EQ(v, std::string("ok"));
    }();
    sync_wait(std::move(runner));
}

int main() {
    TEST_SUITE("timeout");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
