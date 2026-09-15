#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "flowcoro.hpp"
#include "flowcoro/idle_park.h"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using clk = std::chrono::steady_clock;

static double median_ns(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

TEST_CASE(idle_park_wake_one) {
    IdlePark park;
    std::atomic<bool> entered{false};
    std::atomic<bool> left{false};

    std::thread waiter([&] {
        entered.store(true, std::memory_order_release);
        park.wait([&] { return left.load(std::memory_order_acquire); });
    });

    while (!entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto start = clk::now();
    left.store(true, std::memory_order_release);
    park.wake_one();
    waiter.join();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - start).count();

    std::cout << "  IdlePark wake_one latency: " << (ns / 1e6) << " ms\n";
    TEST_EXPECT_TRUE(ns < 50'000'000); // 50ms: parked waiters must be interruptible
}

TEST_CASE(idle_park_stop_unblocks) {
    IdlePark park;
    std::atomic<bool> entered{false};

    std::thread waiter([&] {
        entered.store(true, std::memory_order_release);
        park.wait([] { return false; });
    });

    while (!entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    park.request_stop();
    waiter.join();
    TEST_EXPECT_TRUE(true);
}

TEST_CASE(thread_pool_idle_wakeup) {
    lockfree::ThreadPool pool(1);
    // Let the worker reach the park path.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr int kTrials = 12;
    std::vector<double> samples;
    samples.reserve(kTrials);

    for (int i = 0; i < kTrials; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto start = clk::now();
        auto fut = pool.enqueue([] { return 1; });
        TEST_EXPECT_EQ(fut.get(), 1);
        samples.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - start).count()));
    }

    double med = median_ns(samples);
    std::cout << "  ThreadPool idle wakeup median: " << (med / 1e6) << " ms\n";
    // Old path slept up to 10ms once fully idle. Park must beat that with margin.
    TEST_EXPECT_TRUE(med < 8'000'000);
}

TEST_CASE(thread_pool_hot_burst) {
    lockfree::ThreadPool pool(2);
    std::atomic<int> n{0};
    std::vector<std::future<void>> futs;
    futs.reserve(200);
    for (int i = 0; i < 200; ++i) {
        futs.push_back(pool.enqueue([&n] { n.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (auto& f : futs) {
        f.wait();
    }
    TEST_EXPECT_EQ(n.load(), 200);
}

TEST_CASE(thread_pool_idle_shutdown) {
    auto start = clk::now();
    {
        lockfree::ThreadPool pool(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - start).count();
    std::cout << "  idle ThreadPool dtor: " << ms << " ms\n";
    TEST_EXPECT_TRUE(ms < 1500); // destructor used to wait up to 1s polling sleepers
}

TEST_CASE(coroutine_pool_is_single_scheduler_by_default) {
    TEST_EXPECT_EQ(coroutine_pool_num_schedulers(), static_cast<size_t>(1));
}

TEST_CASE(coroutine_scheduler_idle_wakeup) {
    // Warm the singleton pool so the scheduler thread exists, then let it park.
    sync_wait([]() -> Task<void> {
        co_await yield();
        co_return;
    }());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr int kTrials = 8;
    std::vector<double> samples;
    samples.reserve(kTrials);

    for (int i = 0; i < kTrials; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto start = clk::now();
        int v = sync_wait([]() -> Task<int> {
            co_await yield();
            co_return 7;
        }());
        TEST_EXPECT_EQ(v, 7);
        samples.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - start).count()));
    }

    double med = median_ns(samples);
    std::cout << "  CoroutineScheduler idle wakeup median: " << (med / 1e6) << " ms\n";
    TEST_EXPECT_TRUE(med < 8'000'000);
}

int main() {
    TEST_SUITE("idle park / wakeup");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
