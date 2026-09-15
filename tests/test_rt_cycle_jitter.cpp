// CI-safe RtExecutor cycle jitter lamp.
//
// 宽松墙钟闸门，给共享 runner / sanitizer / MinGW ~15.6ms 时钟留余量。
// 更严的报告在 benchmarks/rt_cycle_jitter_benchmark（FLOWCORO_RT_SLO_STRICT=1）。
//
// 本测试覆盖 host-tick+yield 与跨线程 post_ready，不依赖 PR #20 的 sleep_until。

#include "flowcoro.hpp"
#include "test_framework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

using namespace flowcoro;
using steady = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;
using namespace std::chrono_literals;

namespace {

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' ||
           v[0] == 'y' || v[0] == 'Y';
}

struct Pct {
    std::size_t n = 0;
    ns p50{0};
    ns p99{0};
    ns max{0};
};

Pct pct(std::vector<ns> v) {
    Pct r;
    r.n = v.size();
    if (v.empty()) return r;
    std::sort(v.begin(), v.end());
    r.max = v.back();
    r.p50 = v[v.size() / 2];
    r.p99 = v[(v.size() * 99) / 100];
    if (r.p99 < r.p50) r.p99 = r.p50;
    return r;
}

void print_pct(const char* tag, const Pct& r) {
    auto ms = [](ns x) {
        return std::chrono::duration<double, std::milli>(x).count();
    };
    std::cout << std::fixed << "  [" << tag << "] n=" << r.n
              << "  p50=" << ms(r.p50) << "ms"
              << "  p99=" << ms(r.p99) << "ms"
              << "  max=" << ms(r.max) << "ms\n";
}

struct park_awaiter {
    std::atomic<void*>* out;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        out->store(h.address(), std::memory_order_release);
    }
    void await_resume() noexcept {}
};

rt::RtTask host_tick_worker(int total, int warmup, ns period,
                            steady::time_point origin,
                            std::vector<ns>& intervals, std::vector<ns>& late) {
    steady::time_point prev{};
    bool have_prev = false;
    int i = 0;
    while (i < total) {
        co_await rt::yield();
        ++i;
        const auto now = steady::now();
        if (i <= warmup) continue;
        const auto deadline = origin + period * i;
        auto tardiness = now - deadline;
        if (tardiness < steady::duration::zero()) tardiness = steady::duration::zero();
        late.push_back(std::chrono::duration_cast<ns>(tardiness));
        if (have_prev) {
            intervals.push_back(std::chrono::duration_cast<ns>(now - prev));
        }
        have_prev = true;
        prev = now;
    }
    co_return;
}

rt::RtTask sensor_worker(int total, int warmup,
                         std::atomic<void*>& parked,
                         std::atomic<int64_t>& fire_ns,
                         std::vector<ns>& late) {
    int i = 0;
    while (i < total) {
        co_await park_awaiter{&parked};
        ++i;
        const auto now = steady::now();
        if (i <= warmup) continue;
        const auto fire = steady::time_point(std::chrono::duration_cast<steady::duration>(
            ns(fire_ns.load(std::memory_order_acquire))));
        auto tardiness = now - fire;
        if (tardiness < steady::duration::zero()) tardiness = steady::duration::zero();
        late.push_back(std::chrono::duration_cast<ns>(tardiness));
    }
    co_return;
}

void assert_gates(const Pct& interval, const Pct& late, ns period) {
    const bool strict = env_flag("FLOWCORO_RT_SLO_STRICT");
    const auto p50_lo = strict ? period * 7 / 10 : period / 4;
    const auto p50_hi = strict ? period * 13 / 10 : period * 8;
    TEST_EXPECT_TRUE(interval.n > 0);
    TEST_EXPECT_TRUE(late.n > 0);
    TEST_EXPECT_TRUE(interval.p50 >= p50_lo);
    TEST_EXPECT_TRUE(interval.p50 <= p50_hi);
    TEST_EXPECT_TRUE(late.p99 <= (strict ? 3ms : 200ms));
    TEST_EXPECT_TRUE(late.max <= (strict ? 15ms : 500ms));
}

} // namespace

TEST_CASE(rt_cycle_host_tick_yield_jitter) {
    constexpr auto kPeriod = 10ms;
    constexpr int kWarmup = 3;
    constexpr int kRecorded = 20;
    const int total = kWarmup + kRecorded + 2;

    std::vector<ns> intervals;
    std::vector<ns> late;
    rt::RtExecutor exec;
    const auto origin = steady::now();
    exec.spawn(host_tick_worker(total, kWarmup, kPeriod, origin, intervals, late),
               "host_tick");

    auto next = origin;
    const auto wall = steady::now() + 8s;
    while (!exec.is_finished() && steady::now() < wall) {
        exec.run();
        if (exec.is_finished()) break;
        next += kPeriod;
        const auto now = steady::now();
        if (next > now) std::this_thread::sleep_until(next);
        else next = now;
    }
    if (!exec.is_finished()) exec.shutdown();

    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(late.size() >= static_cast<std::size_t>(kRecorded - 5));
    const auto iv = pct(intervals);
    const auto lt = pct(late);
    std::cout << "host-tick+yield period=10ms\n";
    print_pct("interval", iv);
    print_pct("late    ", lt);
    assert_gates(iv, lt, kPeriod);
}

TEST_CASE(rt_cycle_sensor_post_ready_jitter) {
    constexpr auto kPeriod = 10ms;
    constexpr int kWarmup = 2;
    constexpr int kRecorded = 15;
    const int total = kWarmup + kRecorded;

    std::vector<ns> late;
    std::atomic<void*> parked{nullptr};
    std::atomic<int64_t> fire_ns{0};

    rt::RtExecutor exec;
    exec.spawn(sensor_worker(total, kWarmup, parked, fire_ns, late), "sensor");
    for (int i = 0; i < 2000 && parked.load() == nullptr; ++i) {
        exec.run();
        std::this_thread::sleep_for(100us);
    }
    TEST_EXPECT_TRUE(parked.load() != nullptr);

    const auto origin = steady::now();
    std::thread producer([&] {
        for (int i = 1; i <= total; ++i) {
            const auto fire = origin + kPeriod * i;
            std::this_thread::sleep_until(fire);
            fire_ns.store(std::chrono::duration_cast<ns>(fire.time_since_epoch()).count(),
                          std::memory_order_release);
            const auto wait_dl = steady::now() + 500ms;
            void* addr = nullptr;
            while ((addr = parked.load(std::memory_order_acquire)) == nullptr) {
                if (steady::now() > wait_dl) return;
                std::this_thread::sleep_for(50us);
            }
            parked.store(nullptr, std::memory_order_release);
            exec.post_ready(std::coroutine_handle<>::from_address(addr));
        }
    });

    const auto wall = steady::now() + 8s;
    while (!exec.is_finished() && steady::now() < wall) {
        exec.run();
    }
    producer.join();
    if (!exec.is_finished()) exec.shutdown();

    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(late.size() >= static_cast<std::size_t>(kRecorded - 5));
    const auto lt = pct(late);
    std::cout << "sensor post_ready period=10ms\n";
    print_pct("late    ", lt);
    const bool strict = env_flag("FLOWCORO_RT_SLO_STRICT");
    TEST_EXPECT_TRUE(lt.n > 0);
    TEST_EXPECT_TRUE(lt.p99 <= (strict ? 5ms : 200ms));
    TEST_EXPECT_TRUE(lt.max <= (strict ? 20ms : 500ms));
}

int main() {
    TEST_SUITE("flowcoro::rt cycle jitter (CI-safe)");
    flowcoro::test::TestRunner::print_summary();
    return flowcoro::test::TestRunner::all_passed() ? 0 : 1;
}
