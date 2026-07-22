/**
 * @file test_lockfree_stress.cpp
 * @brief Stress tests for lockfree::Queue / Stack with hazard pointer SMR.
 *
 * Designed to be run under TSAN/ASAN to catch:
 *   - ABA / use-after-free (was the original bug)
 *   - data loss race (the second bug)
 *
 * Pattern: N producers + M consumers hammer the queue concurrently; we then
 * assert that no item was lost or duplicated (sum invariant and count
 * invariant). Consumers exit gracefully once all producers are done AND the
 * queue has been drained — no deadline-based join that could block forever.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "flowcoro.hpp"
#include "flowcoro/hazard_pointer.h"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

namespace {

// Each producer claims a contiguous slice of [1, total] via a shared cursor
// and enqueues each value exactly once.
void producer_worker(lockfree::Queue<int>& q,
                     std::atomic<size_t>& cursor,
                     size_t total,
                     std::atomic<size_t>& producers_done) {
    for (;;) {
        size_t idx = cursor.fetch_add(1, std::memory_order_relaxed);
        if (idx >= total) {
            // Cursor overshot slightly — that's fine, the extra slot is
            // simply never enqueued. Do NOT fetch_sub (causes ABA on cursor).
            break;
        }
        q.enqueue(static_cast<int>(idx + 1));
    }
    producers_done.fetch_add(1, std::memory_order_release);
}

void consumer_worker(lockfree::Queue<int>& q,
                     std::atomic<long long>& sum,
                     std::atomic<size_t>& count,
                     std::atomic<size_t>& producers_done,
                     size_t producer_count,
                     size_t expected_total) {
    int v = 0;
    for (;;) {
        if (q.dequeue(v)) {
            sum.fetch_add(v, std::memory_order_relaxed);
            size_t c = count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (c >= expected_total) return;
        } else {
            // Queue empty: only exit when ALL producers are done AND we've
            // consumed everything they produced. Otherwise yield and retry.
            if (producers_done.load(std::memory_order_acquire) >= producer_count &&
                count.load(std::memory_order_relaxed) >= expected_total) {
                return;
            }
            std::this_thread::yield();
        }
    }
}

} // namespace

// Single-threaded smoke test: enqueue/dequeue FIFO correctness
TEST_CASE(lockfree_queue_basic_fifo) {
    std::cout << "\n=== lockfree::Queue basic FIFO ===\n";
    lockfree::Queue<int> q;
    for (int i = 1; i <= 100; ++i) {
        q.enqueue(i);
    }
    int v = 0;
    int prev = 0;
    int got = 0;
    while (q.dequeue(v)) {
        TEST_EXPECT_EQ(v, prev + 1);
        prev = v;
        ++got;
    }
    TEST_EXPECT_EQ(got, 100);

    // 队列空后再入/出
    q.enqueue(999);
    bool ok = q.dequeue(v);
    TEST_EXPECT_TRUE(ok);
    TEST_EXPECT_EQ(v, 999);
}

// Stack basic LIFO
TEST_CASE(lockfree_stack_basic_lifo) {
    std::cout << "\n=== lockfree::Stack basic LIFO ===\n";
    lockfree::Stack<int> s;
    for (int i = 1; i <= 50; ++i) s.push(i);
    int v = 0;
    int expected = 50;
    while (s.pop(v)) {
        TEST_EXPECT_EQ(v, expected);
        --expected;
    }
    TEST_EXPECT_EQ(expected, 0);
}

// MPMC stress — designed to break the buggy implementation.
TEST_CASE(lockfree_queue_mpmc_stress) {
    std::cout << "\n=== lockfree::Queue MPMC stress (TSAN-friendly) ===\n";
    constexpr size_t PRODUCERS = 4;
    constexpr size_t CONSUMERS = 4;
    constexpr size_t TOTAL = 50000; // bounded workload — still exercises ABA

    lockfree::Queue<int> q;
    std::atomic<size_t> cursor{0};
    std::atomic<size_t> producers_done{0};
    std::atomic<long long> sum{0};
    std::atomic<size_t> count{0};

    long long expected_sum =
        static_cast<long long>(TOTAL) * static_cast<long long>(TOTAL + 1) / 2;

    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    for (size_t i = 0; i < PRODUCERS; ++i) {
        threads.emplace_back(producer_worker, std::ref(q), std::ref(cursor),
                             TOTAL, std::ref(producers_done));
    }
    for (size_t i = 0; i < CONSUMERS; ++i) {
        threads.emplace_back(consumer_worker, std::ref(q), std::ref(sum),
                              std::ref(count), std::ref(producers_done),
                              PRODUCERS, TOTAL);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    size_t got = count.load(std::memory_order_acquire);
    long long got_sum = sum.load(std::memory_order_acquire);

    std::cout << "  produced=" << TOTAL << " consumed=" << got
              << " sum=" << got_sum << " expected_sum=" << expected_sum << "\n";

    TEST_EXPECT_EQ(got, TOTAL);
    TEST_EXPECT_EQ(got_sum, expected_sum);
}

// Stack MPMC stress — Treiber stack with HP
TEST_CASE(lockfree_stack_mpmc_stress) {
    std::cout << "\n=== lockfree::Stack MPMC stress ===\n";
    constexpr size_t PRODUCERS = 4;
    constexpr size_t CONSUMERS = 4;
    constexpr size_t PER_PRODUCER = 5000;
    constexpr size_t TOTAL = PRODUCERS * PER_PRODUCER;

    lockfree::Stack<int> s;
    std::atomic<size_t> producers_done{0};
    std::atomic<long long> sum{0};
    std::atomic<size_t> consumed{0};

    auto producer = [&](size_t id) {
        int base = static_cast<int>(id * PER_PRODUCER);
        for (size_t k = 0; k < PER_PRODUCER; ++k) {
            s.push(base + static_cast<int>(k) + 1);
        }
        producers_done.fetch_add(1, std::memory_order_release);
    };

    auto consumer = [&]() {
        int v = 0;
        for (;;) {
            if (s.pop(v)) {
                sum.fetch_add(v, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_relaxed);
                if (consumed.load(std::memory_order_relaxed) >= TOTAL) return;
            } else {
                if (producers_done.load(std::memory_order_acquire) >= PRODUCERS &&
                    consumed.load(std::memory_order_relaxed) >= TOTAL) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);
    for (size_t i = 0; i < PRODUCERS; ++i) {
        threads.emplace_back(producer, i);
    }
    for (size_t i = 0; i < CONSUMERS; ++i) {
        threads.emplace_back(consumer);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    long long expected_sum = static_cast<long long>(TOTAL) * static_cast<long long>(TOTAL + 1) / 2;
    std::cout << "  consumed=" << consumed.load()
              << " sum=" << sum.load()
              << " expected=" << expected_sum << "\n";
    TEST_EXPECT_EQ(consumed.load(), TOTAL);
    TEST_EXPECT_EQ(sum.load(), expected_sum);
}

int main() {
    TEST_SUITE("lockfree_stress");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
