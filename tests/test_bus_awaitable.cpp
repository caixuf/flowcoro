/**
 * @file test_bus_awaitable.cpp
 * @brief Tests for BusAwaitable — verifies all three bug fixes.
 *
 * Bug 1 — double-resume prevention  (std::atomic_flag fired_)
 * Bug 2 — use-after-free prevention (shared_ptr State + destructor cleanup)
 * Bug 3 — correct acquire/release ordering for the message pointer
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "flowcoro.hpp"
#include "flowcoro/coroutine_task.h"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// ─────────────────────────────────────────────────────────────────────────────
// MockBus — minimal synchronous callback-based message bus for testing
// ─────────────────────────────────────────────────────────────────────────────

struct TestMsg {
    int value;
};

class MockBus {
public:
    using Callback           = void (*)(const TestMsg*, void*);
    using SubscriptionHandle = int;

    SubscriptionHandle subscribe(Callback cb, void* user_data) {
        std::lock_guard<std::mutex> lk(mu_);
        int id = next_id_++;
        subs_.push_back({id, cb, user_data, /*removed=*/false});
        return id;
    }

    // Synchronous unsubscribe: after this returns no further delivery for
    // this id will occur from publish().
    void unsubscribe(SubscriptionHandle id) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_)
            if (s.id == id) { s.removed = true; break; }
    }

    void publish(const TestMsg& msg) {
        // Snapshot active subs while holding the lock, then call without it.
        std::vector<std::pair<Callback, void*>> active;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& s : subs_)
                if (!s.removed) active.emplace_back(s.cb, s.user_data);
        }
        for (auto& [cb, ud] : active) cb(&msg, ud);
    }

    // Fire message from a separate thread after an optional delay.
    void publish_async(const TestMsg& msg,
                       std::chrono::milliseconds delay = {}) {
        std::thread([this, msg, delay] {
            if (delay.count() > 0)
                std::this_thread::sleep_for(delay);
            publish(msg);
        }).detach();
    }

    size_t subscriber_count() const {
        std::lock_guard<std::mutex> lk(mu_);
        size_t n = 0;
        for (auto& s : subs_) n += !s.removed;
        return n;
    }

private:
    struct Sub { int id; Callback cb; void* user_data; bool removed; };
    mutable std::mutex mu_;
    std::vector<Sub>   subs_;
    int                next_id_ = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — basic message delivery
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(bus_awaitable_basic_delivery) {
    std::cout << "  Testing basic message delivery..." << std::endl;

    MockBus bus;

    // Deliver from another thread after a short delay so that the coroutine
    // has time to reach await_suspend before the message arrives.
    bus.publish_async({42}, std::chrono::milliseconds(30));

    auto task = [&]() -> Task<int> {
        BusAwaitable<TestMsg, MockBus> aw(bus);
        const TestMsg& msg = co_await aw;
        co_return msg.value;
    }();

    int result = sync_wait(std::move(task));

    TEST_EXPECT_EQ(result, 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Bug 1: double-resume prevention
//
// Fire two messages from two threads simultaneously after the coroutine has
// suspended.  Only the first delivery may resume the coroutine; the second
// must be silently dropped.  Without the fix this causes UB (resuming a
// non-suspended coroutine) or an incorrect resume-count.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(bus_awaitable_double_resume_prevention) {
    std::cout << "  Testing double-resume prevention (Bug 1)..." << std::endl;

    MockBus bus;
    std::atomic<int> resume_count{0};

    // First message fires after 30 ms from thread A, second from thread B.
    bus.publish_async({1}, std::chrono::milliseconds(30));
    bus.publish_async({2}, std::chrono::milliseconds(30));

    auto task = [&]() -> Task<int> {
        BusAwaitable<TestMsg, MockBus> aw(bus);
        const TestMsg& msg = co_await aw;
        resume_count.fetch_add(1, std::memory_order_acq_rel);
        // Brief pause so a racing second resume would land here if the bug
        // were present.
        co_await sleep_for(std::chrono::milliseconds(20));
        co_return msg.value;
    }();

    int result = sync_wait(std::move(task));

    // Must have been resumed exactly once.
    TEST_EXPECT_EQ(resume_count.load(), 1);
    // Received value must be the winning message (1 or 2), never 0.
    TEST_EXPECT_TRUE(result == 1 || result == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Bug 2: destructor unsubscribes and avoids use-after-free
//
// Create an awaitable, let the coroutine reach await_suspend, then destroy
// the Task (which schedules the coroutine frame for deferred destruction).
// After the scheduler processes the deferred destruction ~BusAwaitable runs
// and unsubscribes.  A subsequent publish must not crash.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(bus_awaitable_destructor_unsubscribes) {
    std::cout << "  Testing destructor cleanup (Bug 2)..." << std::endl;

    MockBus bus;

    {
        auto task = [&]() -> Task<void> {
            BusAwaitable<TestMsg, MockBus> aw(bus);
            co_await aw; // suspends here; Task is dropped before resume
        }();

        // Give the coroutine time to reach await_suspend.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        TEST_EXPECT_EQ(bus.subscriber_count(), static_cast<size_t>(1));

        // task goes out of scope → Task::safe_destroy() schedules the
        // coroutine frame for deferred destruction.
    }

    // Drive the scheduler so it processes the deferred destruction, which
    // calls handle.destroy() → ~BusAwaitable() → bus_.unsubscribe().
    auto& mgr = CoroutineManager::get_instance();
    for (int i = 0; i < 20; ++i) {
        mgr.drive();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    TEST_EXPECT_EQ(bus.subscriber_count(), static_cast<size_t>(0));

    // Publishing now must not crash or access freed memory.
    bus.publish({99});

    TEST_EXPECT_TRUE(true); // reaching here proves no crash/UB
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — Bug 3: message pointer visible to the resuming thread
//
// Deliver from a different thread and verify the exact value was observed.
// Without the release/acquire pair on State::msg the value could be zero or
// stale.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(bus_awaitable_cross_thread_visibility) {
    std::cout << "  Testing cross-thread message visibility (Bug 3)..." << std::endl;

    MockBus bus;

    bus.publish_async({12345}, std::chrono::milliseconds(30));

    auto task = [&]() -> Task<int> {
        BusAwaitable<TestMsg, MockBus> aw(bus);
        const TestMsg& msg = co_await aw;
        co_return msg.value;
    }();

    int result = sync_wait(std::move(task));

    TEST_EXPECT_EQ(result, 12345);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — sequential awaits: each await subscribes and unsubscribes cleanly
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(bus_awaitable_sequential_awaits) {
    std::cout << "  Testing sequential awaits..." << std::endl;

    MockBus bus;

    // Schedule three messages with staggered delays so they arrive one per
    // await.  Each subsequent message must arrive after the previous resume
    // has completed (coroutine is again at await_suspend).
    bus.publish_async({10}, std::chrono::milliseconds(30));
    bus.publish_async({20}, std::chrono::milliseconds(120));
    bus.publish_async({30}, std::chrono::milliseconds(210));

    auto task = [&]() -> Task<int> {
        int sum = 0;
        for (int i = 0; i < 3; ++i) {
            BusAwaitable<TestMsg, MockBus> aw(bus);
            const TestMsg& msg = co_await aw;
            sum += msg.value;
            // Pause so the next message definitely arrives at the next await.
            co_await sleep_for(std::chrono::milliseconds(50));
        }
        co_return sum;
    }();

    int result = sync_wait(std::move(task));

    TEST_EXPECT_EQ(result, 60);
    // All subscriptions must have been cleaned up.
    TEST_EXPECT_EQ(bus.subscriber_count(), static_cast<size_t>(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    TEST_SUITE("BusAwaitable Tests");

    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
