// flowcoro::rt 周期控制回路延迟 / 抖动 SLO 灯笼。
//
// 测的是「sleep_until 对齐周期」和「宿主 run() + yield」两条 FlowEngine 式路径
// 的 tick-to-tick 间隔与 deadline tardiness (max(0, now - deadline))。
//
// CI 阈值刻意宽松: 共享 runner / MinGW 15.6ms 时钟 / sanitizer 都会把抖动拉大。
// 本机隔离核上的更严预算:
//
//   FLOWCORO_RT_SLO_STRICT=1 ctest -R test_rt_latency --output-on-failure
//
// 严格模式默认: 10ms 周期下 p99 tardiness <= 3ms, max <= 15ms,
// p50 tick 间隔落在 [7ms, 13ms]。这仍不是微秒级硬实时宣称, 只是回归闸门。

#include "flowcoro.hpp"
#include "test_framework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

using namespace flowcoro;
using namespace std::chrono_literals;

namespace {

using clock = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' ||
           v[0] == 'y' || v[0] == 'Y';
}

void print_report(std::string_view tag, const rt::JitterReport& r) {
    std::cout << std::fixed << std::setprecision(3)
              << "  [" << tag << "] n=" << r.n
              << "  p50=" << rt::ns_to_ms(r.p50) << "ms"
              << "  p99=" << rt::ns_to_ms(r.p99) << "ms"
              << "  max=" << rt::ns_to_ms(r.max) << "ms"
              << "  mean=" << rt::ns_to_ms(r.mean) << "ms\n";
}

// 宿主: run() + 睡到 next_timer_deadline(或 1ms 兜底)。不盲睡。
bool drive_with_timers(rt::RtExecutor& exec, clock::time_point wall_deadline) {
    while (!exec.is_finished()) {
        exec.run();
        if (exec.is_finished()) break;
        if (clock::now() > wall_deadline) return false;
        if (exec.has_local_work()) continue;
        if (auto next = exec.next_timer_deadline()) {
            std::this_thread::sleep_until(*next);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return exec.is_finished();
}

rt::RtTask deadline_slo_worker(std::chrono::milliseconds period,
                               int total_ticks,
                               int warmup,
                               std::vector<ns>& intervals,
                               std::vector<ns>& late) {
    const auto origin = clock::now();
    clock::time_point prev{};
    bool have_prev = false;
    for (int i = 1; i <= total_ticks; ++i) {
        const auto deadline = origin + period * i;
        co_await rt::sleep_until(deadline);
        const auto now = clock::now();
        if (i > warmup) {
            auto tardiness = now - deadline;
            if (tardiness < clock::duration::zero()) {
                tardiness = clock::duration::zero();
            }
            late.push_back(std::chrono::duration_cast<ns>(tardiness));
            if (have_prev) {
                intervals.push_back(std::chrono::duration_cast<ns>(now - prev));
            }
        }
        have_prev = (i > warmup);
        prev = now;
    }
    co_return;
}

rt::RtTask yield_slo_worker(int total_ticks,
                            int warmup,
                            std::vector<ns>& intervals) {
    clock::time_point prev{};
    bool have_prev = false;
    for (int i = 0; i < total_ticks; ++i) {
        const auto now = clock::now();
        if (i >= warmup && have_prev) {
            intervals.push_back(std::chrono::duration_cast<ns>(now - prev));
        }
        if (i >= warmup) {
            have_prev = true;
            prev = now;
        }
        co_await rt::yield();
    }
    co_return;
}

void assert_report_ordered(const rt::JitterReport& r) {
    TEST_EXPECT_TRUE(r.n > 0);
    TEST_EXPECT_TRUE(r.p50 <= r.p99);
    TEST_EXPECT_TRUE(r.p99 <= r.max);
}

} // namespace

// ---------------------------------------------------------------------------
// sleep_until 对齐周期: 记录 tick 间隔与 tardiness, 打 CI/严格两套闸。
// ---------------------------------------------------------------------------
TEST_CASE(rt_slo_sleep_until_deadline_jitter) {
    constexpr auto kPeriod = 10ms;
    constexpr int kWarmup = 5;
    constexpr int kRecorded = 80;
    const int total = kWarmup + kRecorded + 1;  // +1: 间隔比 tardiness 少 1

    std::vector<ns> intervals;
    std::vector<ns> late;
    intervals.reserve(static_cast<std::size_t>(kRecorded));
    late.reserve(static_cast<std::size_t>(kRecorded));

    rt::RtExecutor exec;
    exec.spawn(deadline_slo_worker(kPeriod, total, kWarmup, intervals, late),
               "slo");

    TEST_EXPECT_TRUE(drive_with_timers(exec, clock::now() + 8s));
    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(late.size() >= static_cast<std::size_t>(kRecorded - 2));
    TEST_EXPECT_TRUE(intervals.size() >= static_cast<std::size_t>(kRecorded - 3));

    const auto iv = rt::jitter_report(intervals);
    const auto lt = rt::jitter_report(late);
    std::cout << "rt_slo sleep_until period=" << kPeriod.count() << "ms\n";
    print_report("interval", iv);
    print_report("late    ", lt);
    assert_report_ordered(iv);
    assert_report_ordered(lt);

    const bool strict = env_flag("FLOWCORO_RT_SLO_STRICT");
    const auto p50_lo = strict ? kPeriod * 7 / 10 : kPeriod / 4;
    const auto p50_hi = strict ? kPeriod * 13 / 10 : kPeriod * 8;
    TEST_EXPECT_TRUE(iv.p50 >= p50_lo);
    TEST_EXPECT_TRUE(iv.p50 <= p50_hi);

    const auto p99_late_lim = strict ? 3ms : 200ms;
    const auto max_late_lim = strict ? 15ms : 500ms;
    TEST_EXPECT_TRUE(lt.p99 <= p99_late_lim);
    TEST_EXPECT_TRUE(lt.max <= max_late_lim);
}

// ---------------------------------------------------------------------------
// 宿主按 period sleep_until 调 run(); 任务每 tick yield 一次。测宿主节拍。
// ---------------------------------------------------------------------------
TEST_CASE(rt_slo_host_tick_yield_jitter) {
    constexpr auto kPeriod = 10ms;
    constexpr int kWarmup = 5;
    constexpr int kRecorded = 60;
    const int total = kWarmup + kRecorded + 2;

    std::vector<ns> intervals;
    intervals.reserve(static_cast<std::size_t>(kRecorded));

    rt::RtExecutor exec;
    exec.spawn(yield_slo_worker(total, kWarmup, intervals), "host_tick");

    auto next = clock::now();
    const auto wall = clock::now() + 8s;
    while (!exec.is_finished() && clock::now() < wall) {
        exec.run();
        if (exec.is_finished()) break;
        next += kPeriod;
        const auto now = clock::now();
        if (next > now) std::this_thread::sleep_until(next);
        else next = now;  // 过载: 不堆积
    }
    if (!exec.is_finished()) exec.shutdown();

    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(intervals.size() >= static_cast<std::size_t>(kRecorded - 5));

    const auto iv = rt::jitter_report(intervals);
    std::cout << "rt_slo host-tick+yield period=" << kPeriod.count() << "ms\n";
    print_report("interval", iv);
    assert_report_ordered(iv);

    const bool strict = env_flag("FLOWCORO_RT_SLO_STRICT");
    const auto p50_lo = strict ? kPeriod * 7 / 10 : kPeriod / 4;
    const auto p50_hi = strict ? kPeriod * 13 / 10 : kPeriod * 8;
    TEST_EXPECT_TRUE(iv.p50 >= p50_lo);
    TEST_EXPECT_TRUE(iv.p50 <= p50_hi);
    TEST_EXPECT_TRUE(iv.max <= (strict ? 20ms : 500ms));
}

TEST_CASE(rt_jitter_report_percentiles) {
    std::vector<ns> s;
    for (int i = 1; i <= 100; ++i) s.emplace_back(std::chrono::milliseconds(i));
    const auto r = rt::jitter_report(s);
    TEST_EXPECT_EQ(r.n, static_cast<std::size_t>(100));
    TEST_EXPECT_EQ(r.max, ns(100ms));
    TEST_EXPECT_TRUE(r.p50 >= ns(50ms) && r.p50 <= ns(51ms));
    TEST_EXPECT_TRUE(r.p99 >= ns(99ms) && r.p99 <= ns(100ms));
    TEST_EXPECT_TRUE(r.mean >= ns(50ms) && r.mean <= ns(51ms));
}

int main() {
    TEST_SUITE("flowcoro::rt latency/jitter SLO");
    flowcoro::test::TestRunner::print_summary();
    return flowcoro::test::TestRunner::all_passed() ? 0 : 1;
}
