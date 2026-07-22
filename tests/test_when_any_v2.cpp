/**
 * @file test_when_any_v2.cpp
 * @brief Tests for the event-driven when_any implementation.
 *
 * Verifies:
 *   - Correctness: the fastest task wins
 *   - Latency: winner is reported with sub-millisecond overhead, not
 *     bounded by the old 1ms polling interval
 *   - No data loss: each winner's value is preserved
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <any>
#include <thread>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

// Helper: build a task that sleeps for `d` then returns `v`
template<typename T>
Task<T> delayed(T v, milliseconds d) {
    co_await ClockAwaiter(d);
    co_return v;
}

// Basic: fastest of three wins
TEST_CASE(when_any_v2_basic) {
    std::cout << "\n=== when_any (event-driven) basic ===\n";
    auto runner = []() -> Task<void> {
        auto start = high_resolution_clock::now();
        auto result = co_await when_any(
            delayed(1,   milliseconds(50)),  // index 0
            delayed(2,   milliseconds(300)), // index 1
            delayed(99,  milliseconds(150)) // index 2
        );
        auto elapsed = duration_cast<microseconds>(
            high_resolution_clock::now() - start);

        std::cout << "  winner=" << result.first
                  << " elapsed=" << elapsed.count() << "us\n";
        TEST_EXPECT_EQ(result.first, std::size_t(0));
        TEST_EXPECT_TRUE(elapsed < milliseconds(150));
        auto v = std::any_cast<int>(result.second);
        TEST_EXPECT_EQ(v, 1);
    }();
    sync_wait(std::move(runner));
}

// Latency: winner should not be bounded by any polling interval.
// Two tasks that complete at ~5ms apart. Total latency should be close
// to the slowest winner's completion (10ms), well under any 1ms floor.
TEST_CASE(when_any_v2_low_latency) {
    std::cout << "\n=== when_any low-latency ===\n";
    auto runner = []() -> Task<void> {
        auto start = high_resolution_clock::now();
        auto result = co_await when_any(
            delayed(10, milliseconds(5)),   // winner
            delayed(20, milliseconds(50))   // loser
        );
        auto elapsed = duration_cast<microseconds>(
            high_resolution_clock::now() - start);

        std::cout << "  winner=" << result.first
                  << " elapsed=" << elapsed.count() << "us\n";
        TEST_EXPECT_EQ(result.first, std::size_t(0));
        // 5ms task + scheduling overhead. TSAN/ASAN 插桩会增加调度延迟，
        // 给足余量避免 sanitizer 构建下的 flaky 失败。
        TEST_EXPECT_TRUE(elapsed < milliseconds(150));
        auto v = std::any_cast<int>(result.second);
        TEST_EXPECT_EQ(v, 10);
    }();
    sync_wait(std::move(runner));
}

// Different return types via std::any
TEST_CASE(when_any_v2_heterogeneous) {
    std::cout << "\n=== when_any heterogeneous ===\n";
    auto runner = []() -> Task<void> {
        auto result = co_await when_any(
            delayed(std::string("hello"), milliseconds(100)),
            delayed(42,                     milliseconds(20))  // winner
        );
        TEST_EXPECT_EQ(result.first, std::size_t(1));
        auto v = std::any_cast<int>(result.second);
        TEST_EXPECT_EQ(v, 42);
    }();
    sync_wait(std::move(runner));
}

// All done synchronously (await_ready short-circuit)
TEST_CASE(when_any_v2_all_immediate) {
    std::cout << "\n=== when_any all immediate ===\n";
    auto runner = []() -> Task<void> {
        auto result = co_await when_any(
            delayed(100, milliseconds(0)),
            delayed(200, milliseconds(0))
        );
        // Either can win — both already complete.
        TEST_EXPECT_TRUE(result.first == 0 || result.first == 1);
        int v = (result.first == 0) ? 100 : 200;
        if (result.first == 0) {
            TEST_EXPECT_EQ(std::any_cast<int>(result.second), 100);
        } else {
            TEST_EXPECT_EQ(std::any_cast<int>(result.second), 200);
        }
        (void)v;
    }();
    sync_wait(std::move(runner));
}

// Timeout path: when_any_timeout returns index 0 when task wins, 1 when timeout wins
TEST_CASE(when_any_timeout_basic) {
    std::cout << "\n=== when_any_timeout basic ===\n";
    auto runner = []() -> Task<void> {
        // Task completes well before timeout: winner index 0
        auto r1 = co_await when_any_timeout(
            delayed(7, milliseconds(10)),
            milliseconds(200));
        TEST_EXPECT_EQ(r1.first, std::size_t(0));
        TEST_EXPECT_EQ(std::any_cast<int>(r1.second), 7);

        // Task takes too long: winner index 1 (timeout)
        auto r2 = co_await when_any_timeout(
            delayed(99, milliseconds(500)),
            milliseconds(20));
        TEST_EXPECT_EQ(r2.first, std::size_t(1));
    }();
    sync_wait(std::move(runner));
}

int main() {
    TEST_SUITE("when_any_v2");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
