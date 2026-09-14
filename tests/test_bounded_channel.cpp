/**
 * @file test_bounded_channel.cpp
 * @brief BoundedChannel 回归测试：容量硬上界 / MPMC 无丢失无重复 / close 语义。
 *
 * 设计意图（配合 TSAN/ASAN 跑）：
 *   - BoundedChannel 是自研的 Vyukov 有界 MPMC 环，唯一的新算法风险点。
 *     这里用「N 生产 + M 消费 + sum/count 不变量」压它，并额外用一个监控线程
 *     持续采样 size()，断言**任何时刻** size() <= capacity()（有界性的硬承诺）。
 *   - 覆盖非 trivially-copyable 载荷（std::string）：slot 是裸存储 + 手工
 *     构造/析构，最容易出悬垂或重复析构的地方。
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "flowcoro/bounded_channel.h"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

namespace {

// 生产者：claim 全局游标拿到唯一下标，直到超出 total。
void push_worker(BoundedChannel<int>& ch,
                 std::atomic<size_t>& cursor,
                 size_t total) {
    for (;;) {
        const size_t idx = cursor.fetch_add(1, std::memory_order_relaxed);
        if (idx >= total) break;
        const int value = static_cast<int>(idx + 1);
        // 满时自旋重试，保证不丢数据（压的是有界队列的正确性，不是丢弃策略）
        while (!ch.try_push(value)) {
            std::this_thread::yield();
        }
    }
}

void pop_worker(BoundedChannel<int>& ch,
                std::atomic<long long>& sum,
                std::atomic<size_t>& count,
                std::atomic<size_t>& producers_done,
                size_t expected_total) {
    int v = 0;
    for (;;) {
        if (ch.try_pop(v)) {
            sum.fetch_add(v, std::memory_order_relaxed);
            const size_t c = count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (c >= expected_total) return;
        } else if (producers_done.load(std::memory_order_acquire) >= 1 &&
                   count.load(std::memory_order_relaxed) >= expected_total) {
            return;
        } else {
            std::this_thread::yield();
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 基础语义
// ---------------------------------------------------------------------------

TEST_CASE(bounded_channel_capacity_is_rounded_up_to_power_of_two) {
    std::cout << "\n=== capacity 向上取 2 的幂 ===\n";
    BoundedChannel<int> c(10000);
    TEST_EXPECT_EQ(c.capacity(), static_cast<size_t>(16384));
    TEST_EXPECT_EQ(c.size(), static_cast<size_t>(0));
    TEST_EXPECT_TRUE(c.empty());
    TEST_EXPECT_FALSE(c.is_closed());

    BoundedChannel<int> exact(1024);
    TEST_EXPECT_EQ(exact.capacity(), static_cast<size_t>(1024));

    bool threw = false;
    try {
        BoundedChannel<int> bad(0);
        (void)bad;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_EXPECT_TRUE(threw);
}

TEST_CASE(bounded_channel_fifo_and_full_empty_semantics) {
    std::cout << "\n=== FIFO 顺序 + 满/空语义 ===\n";
    BoundedChannel<int> c(4);
    TEST_EXPECT_EQ(c.capacity(), static_cast<size_t>(4));

    for (int i = 1; i <= 4; ++i) {
        TEST_EXPECT_TRUE(c.try_push(i));
    }
    TEST_EXPECT_EQ(c.size(), static_cast<size_t>(4));

    // 满：push 必须失败，且不消费传入值
    TEST_EXPECT_FALSE(c.try_push(99));
    TEST_EXPECT_EQ(c.size(), static_cast<size_t>(4));

    // FIFO 出队
    int v = -1;
    for (int i = 1; i <= 4; ++i) {
        TEST_EXPECT_TRUE(c.try_pop(v));
        TEST_EXPECT_EQ(v, i);
    }
    TEST_EXPECT_EQ(c.size(), static_cast<size_t>(0));

    // 空：pop 失败且不修改 out
    v = 12345;
    TEST_EXPECT_FALSE(c.try_pop(v));
    TEST_EXPECT_EQ(v, 12345);

    // 空位可复用（环回：确认 sequence 前进一整轮后 slot 被正确交还）
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 4; ++i) TEST_EXPECT_TRUE(c.try_push(round * 10 + i));
        for (int i = 0; i < 4; ++i) {
            TEST_EXPECT_TRUE(c.try_pop(v));
            TEST_EXPECT_EQ(v, round * 10 + i);
        }
    }
}

TEST_CASE(bounded_channel_close_semantics) {
    std::cout << "\n=== close 语义 ===\n";
    BoundedChannel<int> c(8);
    TEST_EXPECT_TRUE(c.try_push(1));
    TEST_EXPECT_TRUE(c.try_push(2));

    c.close();
    TEST_EXPECT_TRUE(c.is_closed());

    // close 后拒绝新元素
    TEST_EXPECT_FALSE(c.try_push(3));

    // 已有元素仍可 drain 干净
    int v = 0;
    TEST_EXPECT_TRUE(c.try_pop(v));
    TEST_EXPECT_EQ(v, 1);
    TEST_EXPECT_TRUE(c.try_pop(v));
    TEST_EXPECT_EQ(v, 2);
    TEST_EXPECT_FALSE(c.try_pop(v));

    // close 幂等
    c.close();
    TEST_EXPECT_TRUE(c.is_closed());
}

TEST_CASE(bounded_channel_string_payload_move_semantics) {
    std::cout << "\n=== std::string 载荷（非 trivially copyable）===\n";
    BoundedChannel<std::string> c(8);
    TEST_EXPECT_EQ(c.capacity(), static_cast<size_t>(8));

    const std::string long_payload(4096, 'x');
    TEST_EXPECT_TRUE(c.try_push(long_payload));
    TEST_EXPECT_TRUE(c.try_push(std::string("short")));

    std::string out;
    TEST_EXPECT_TRUE(c.try_pop(out));
    TEST_EXPECT_EQ(out.size(), long_payload.size());
    TEST_EXPECT_TRUE(out == long_payload);
    TEST_EXPECT_TRUE(c.try_pop(out));
    TEST_EXPECT_TRUE(out == std::string("short"));

    // 多轮复用，确认 slot 里的对象被析构/重建而不是被复用出脏值
    for (int round = 0; round < 200; ++round) {
        const std::string s = "round-" + std::to_string(round) + "-" + std::string(64, 'y');
        TEST_EXPECT_TRUE(c.try_push(s));
        std::string got;
        TEST_EXPECT_TRUE(c.try_pop(got));
        TEST_EXPECT_TRUE(got == s);
    }
    TEST_EXPECT_EQ(c.size(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// MPMC 压测
// ---------------------------------------------------------------------------

TEST_CASE(bounded_channel_mpmc_stress_no_loss_capacity_hard_bound) {
    std::cout << "\n=== MPMC 压测：4 生产 x 4 消费，容量硬上界 ===\n";
    constexpr size_t PRODUCERS = 4;
    constexpr size_t CONSUMERS = 4;
    constexpr size_t TOTAL = 200000;
    constexpr size_t CAPACITY = 1024;

    BoundedChannel<int> ch(CAPACITY);
    std::atomic<size_t> cursor{0};
    std::atomic<size_t> producers_done{0};
    std::atomic<long long> sum{0};
    std::atomic<size_t> count{0};
    std::atomic<bool> stop_monitor{false};
    std::atomic<size_t> max_observed_size{0};

    const long long expected_sum =
        static_cast<long long>(TOTAL) * static_cast<long long>(TOTAL + 1) / 2;

    // 监控线程：持续采样 size()，验证有界性承诺
    std::thread monitor([&] {
        while (!stop_monitor.load(std::memory_order_acquire)) {
            const size_t s = ch.size();
            size_t prev = max_observed_size.load(std::memory_order_relaxed);
            while (s > prev &&
                   !max_observed_size.compare_exchange_weak(
                       prev, s, std::memory_order_relaxed)) {
            }
        }
    });

    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);
    for (size_t i = 0; i < PRODUCERS; ++i) {
        threads.emplace_back([&] {
            push_worker(ch, cursor, TOTAL);
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }
    for (size_t i = 0; i < CONSUMERS; ++i) {
        threads.emplace_back(pop_worker, std::ref(ch), std::ref(sum),
                             std::ref(count), std::ref(producers_done), TOTAL);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    stop_monitor.store(true, std::memory_order_release);
    if (monitor.joinable()) monitor.join();

    const size_t got = count.load(std::memory_order_acquire);
    const long long got_sum = sum.load(std::memory_order_acquire);
    const size_t peak = max_observed_size.load();

    std::cout << "  produced=" << TOTAL << " consumed=" << got
              << " sum=" << got_sum << " expected_sum=" << expected_sum
              << " peak_size=" << peak << " capacity=" << ch.capacity() << "\n";

    TEST_EXPECT_EQ(got, TOTAL);
    TEST_EXPECT_EQ(got_sum, expected_sum);
    TEST_EXPECT_EQ(ch.size(), static_cast<size_t>(0));
    // 有界性硬承诺：任何时刻不得超过 capacity
    TEST_EXPECT_TRUE(peak <= ch.capacity());
}

int main() {
    TEST_SUITE("bounded_channel");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
