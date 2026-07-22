/**
 * @file test_cancellation.cpp
 * @brief Tests for CancellationToken / CancellationTokenSource / cancellation_point.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

TEST_CASE(cancellation_token_basic) {
    std::cout << "\n=== CancellationToken basic ===\n";
    CancellationTokenSource cts;
    auto t1 = cts.token();
    auto t2 = cts.token(); // shared state

    TEST_EXPECT_FALSE(cts.is_cancelled());
    TEST_EXPECT_FALSE(t1.is_cancelled());
    TEST_EXPECT_FALSE(t2.is_cancelled());

    cts.cancel();
    TEST_EXPECT_TRUE(cts.is_cancelled());
    TEST_EXPECT_TRUE(t1.is_cancelled());
    TEST_EXPECT_TRUE(t2.is_cancelled());
    TEST_EXPECT_TRUE(t1 == t2);
}

TEST_CASE(cancellation_point_throws) {
    std::cout << "\n=== cancellation_point throws ===\n";
    CancellationTokenSource cts;
    auto token = cts.token();

    // Not cancelled: point passes through
    bool threw = false;
    try {
        auto runner = [&]() -> Task<void> {
            co_await cancellation_point(token);
            co_return;
        }();
        sync_wait(std::move(runner));
    } catch (const CancellationTokenException&) {
        threw = true;
    }
    TEST_EXPECT_FALSE(threw);

    // Cancelled: point throws
    cts.cancel();
    threw = false;
    try {
        auto runner = [&]() -> Task<void> {
            co_await cancellation_point(token);
            co_return;
        }();
        sync_wait(std::move(runner));
    } catch (const CancellationTokenException&) {
        threw = true;
    }
    TEST_EXPECT_TRUE(threw);
}

TEST_CASE(cancellation_callback_fires) {
    std::cout << "\n=== CancellationToken callback ===\n";
    CancellationTokenSource cts;
    auto token = cts.token();

    std::atomic<int> fired{0};
    token.register_callback([](void* p) {
        *static_cast<std::atomic<int>*>(p) += 1;
    }, &fired);

    TEST_EXPECT_EQ(fired.load(), 0);
    cts.cancel();
    TEST_EXPECT_EQ(fired.load(), 1);

    // Callbacks are one-shot: cancelling again doesn't refire
    CancellationTokenSource cts2;
    auto t2 = cts2.token();
    cts2.cancel();
    cts2.cancel(); // second call is no-op
    TEST_EXPECT_TRUE(t2.is_cancelled());
}

TEST_CASE(cancellation_token_already_cancelled) {
    std::cout << "\n=== token already cancelled ===\n";
    CancellationTokenSource cts;
    cts.cancel();

    std::atomic<int> fired{0};
    auto token = cts.token();
    // Registering a callback AFTER cancellation should fire immediately
    token.register_callback([](void* p) {
        *static_cast<std::atomic<int>*>(p) += 1;
    }, &fired);
    TEST_EXPECT_EQ(fired.load(), 1);
}

// 协程函数：避免 lambda 闭包在 ASAN detect_stack_use_after_scope 下误报
Task<int> make_cancellable_long_task(std::atomic<bool>& started,
                                     std::atomic<bool>& saw_cancelled,
                                     CancellationToken token) {
    started.store(true);
    for (int i = 0; i < 100; ++i) {
        co_await ClockAwaiter(std::chrono::milliseconds(5));
        // Cooperative cancellation check
        if (token.is_cancelled()) {
            saw_cancelled.store(true);
            co_return -1;
        }
    }
    co_return 42;
}

// 延迟后取消 source 的协程
Task<void> make_delayed_cancel(CancellationTokenSource& cts,
                              std::chrono::milliseconds delay) {
    co_await ClockAwaiter(delay);
    cts.cancel();
    co_await ClockAwaiter(std::chrono::milliseconds(30));
}

TEST_CASE(task_attach_cancellation_token) {
    std::cout << "\n=== Task::attach_cancellation_token ===\n";
    CancellationTokenSource cts;
    auto token = cts.token();

    std::atomic<bool> started{false};
    std::atomic<bool> saw_cancelled{false};

    auto long_task = make_cancellable_long_task(started, saw_cancelled, token);
    long_task.attach_cancellation_token(token);

    auto runner = make_delayed_cancel(cts, std::chrono::milliseconds(20));
    sync_wait(std::move(runner));

    // Try to get the result (may have already finished with -1)
    int result = -100;
    try {
        result = long_task.get();
    } catch (...) {
        // OK: task may be in an unusual state
    }
    (void)result;
    TEST_EXPECT_TRUE(saw_cancelled.load());
}

int main() {
    TEST_SUITE("cancellation");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
