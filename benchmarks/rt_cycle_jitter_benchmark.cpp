/**
 * FlowEngine 风格周期控制回路抖动测量（RtExecutor）。
 *
 * 三条路径（互补 PR #20 的 sleep_until 灯笼；本文件只使用 main 上已有 API）:
 *   1) host-tick + yield     — 宿主 sleep_until 对齐绝对周期，任务 co_await yield()
 *   2) sleep_for 相对周期    — 会漂移；宿主轮询 run()（当前 main 没有 next_timer_deadline）
 *   3) sensor post_ready     — 生产者线程按周期 post_ready，模拟跨线程传感器
 *
 * 报告 period error（tick 间隔）与 tardiness = max(0, now - deadline) 的 p50/p99/max。
 *
 * 用法:
 *   ./rt_cycle_jitter_benchmark
 *   ./rt_cycle_jitter_benchmark --period-us 10000 --duration-ms 2000 --pin -1
 *   FLOWCORO_RT_SLO_STRICT=1 ./rt_cycle_jitter_benchmark
 *
 * 严格模式只打印是否穿过本机闸门，bench 本身不因此失败（失败留给 test）。
 */

#include "flowcoro.hpp"
#include "bench_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace flowcoro;
using steady = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;

namespace {

struct Options {
    int period_us = 10000;
    int duration_ms = 2000;
    int warmup = 5;
    int pin_cpu = -1;
    bool strict = false;
};

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' ||
           v[0] == 'y' || v[0] == 'Y';
}

int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

void apply_env(Options& o) {
    o.period_us = env_int("FLOWCORO_RT_JITTER_PERIOD_US", o.period_us);
    o.duration_ms = env_int("FLOWCORO_RT_JITTER_DURATION_MS", o.duration_ms);
    o.pin_cpu = env_int("FLOWCORO_RT_JITTER_PIN_CPU", o.pin_cpu);
    o.strict = env_flag("FLOWCORO_RT_SLO_STRICT") || env_flag("FLOWCORO_RT_JITTER_STRICT");
}

bool parse_args(int argc, char** argv, Options& o) {
    apply_env(o);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](int& dst) {
            if (i + 1 >= argc) return false;
            dst = std::stoi(argv[++i]);
            return true;
        };
        if (a == "--period-us") {
            if (!need(o.period_us)) return false;
        } else if (a == "--duration-ms") {
            if (!need(o.duration_ms)) return false;
        } else if (a == "--warmup") {
            if (!need(o.warmup)) return false;
        } else if (a == "--pin") {
            if (!need(o.pin_cpu)) return false;
        } else if (a == "--strict") {
            o.strict = true;
        } else if (a == "-h" || a == "--help") {
            return false;
        } else {
            std::cerr << "unknown arg: " << a << "\n";
            return false;
        }
    }
    if (o.period_us < 100) o.period_us = 100;
    if (o.duration_ms < 50) o.duration_ms = 50;
    if (o.warmup < 0) o.warmup = 0;
    return true;
}

void print_usage() {
    std::cout
        << "Usage: rt_cycle_jitter_benchmark [options]\n"
        << "  --period-us N     control period (default 10000 = 10ms)\n"
        << "  --duration-ms N   wall time per scenario (default 2000)\n"
        << "  --warmup N        discarded ticks (default 5)\n"
        << "  --pin N           pin host thread to logical CPU N (-1 = off)\n"
        << "  --strict          print local SLO gate (p99 late <= 3ms @ 10ms)\n"
        << "\nEnv: FLOWCORO_RT_SLO_STRICT=1  FLOWCORO_RT_JITTER_PERIOD_US\n"
        << "     FLOWCORO_RT_JITTER_DURATION_MS  FLOWCORO_RT_JITTER_PIN_CPU\n";
}

void tiny_work() {
    volatile uint32_t acc = 0;
    for (uint32_t i = 0; i < 64; ++i) acc += i * 3u + 1u;
    static_cast<void>(acc);
}

void print_report(const char* tag, const bench::PercentileNs& r) {
    std::cout << std::fixed << std::setprecision(3)
              << "  [" << tag << "] n=" << r.n
              << "  p50=" << bench::ns_to_ms(r.p50_ns) << " ms"
              << "  p99=" << bench::ns_to_ms(r.p99_ns) << " ms"
              << "  max=" << bench::ns_to_ms(r.max_ns) << " ms"
              << "  mean=" << bench::ns_to_ms(r.mean_ns) << " ms\n";
}

void maybe_slo(const Options& o, const char* path,
               const bench::PercentileNs& interval,
               const bench::PercentileNs& late) {
    const double period_ms = o.period_us / 1000.0;
    std::cout << "  period=" << period_ms << " ms  path=" << path << "\n";
    print_report("interval", interval);
    print_report("late    ", late);
    if (!o.strict) return;
    // 与 PR #20 test_rt_latency 严格闸门同口径，仅打印，不 exit。
    const bool p50_ok = interval.p50_ns >= period_ms * 0.7 * 1e6
                        && interval.p50_ns <= period_ms * 1.3 * 1e6;
    const bool late_ok = late.p99_ns <= 3e6 && late.max_ns <= 15e6;
    std::cout << "  STRICT gate (informational): p50_interval "
              << (p50_ok ? "PASS" : "MISS")
              << "  p99/max late " << (late_ok ? "PASS" : "MISS")
              << "  (not a hard-realtime certificate)\n";
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
        tiny_work();
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

rt::RtTask sleep_for_worker(int total, int warmup, ns period,
                            steady::time_point origin,
                            std::vector<ns>& intervals, std::vector<ns>& late) {
    steady::time_point prev{};
    bool have_prev = false;
    for (int i = 1; i <= total; ++i) {
        co_await rt::sleep_for(period);
        tiny_work();
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
                         std::atomic<bool>& stop,
                         std::vector<ns>& intervals, std::vector<ns>& late) {
    steady::time_point prev{};
    bool have_prev = false;
    int i = 0;
    while (i < total && !stop.load(std::memory_order_acquire)) {
        co_await park_awaiter{&parked};
        tiny_work();
        ++i;
        const auto now = steady::now();
        if (i <= warmup) continue;
        const auto fire = steady::time_point(std::chrono::duration_cast<steady::duration>(
            ns(fire_ns.load(std::memory_order_acquire))));
        auto tardiness = now - fire;
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

int ticks_for(const Options& o) {
    const int n = static_cast<int>(
        (static_cast<int64_t>(o.duration_ms) * 1000) / o.period_us);
    return std::max(o.warmup + 8, n);
}

void run_host_tick(const Options& o) {
    const ns period{std::chrono::microseconds(o.period_us)};
    const int total = ticks_for(o);
    std::vector<ns> intervals;
    std::vector<ns> late;
    intervals.reserve(static_cast<size_t>(total));
    late.reserve(static_cast<size_t>(total));

    rt::RtExecutor exec;
    if (o.pin_cpu >= 0) pin_current_thread_to_cpu(o.pin_cpu);

    const auto origin = steady::now();
    exec.spawn(host_tick_worker(total, o.warmup, period, origin, intervals, late),
               "host_tick");

    auto next = origin;
    const auto wall = origin + std::chrono::milliseconds(o.duration_ms + 1000);
    while (!exec.is_finished() && steady::now() < wall) {
        exec.run();
        if (exec.is_finished()) break;
        next += period;
        const auto now = steady::now();
        if (next > now) std::this_thread::sleep_until(next);
        else next = now;
    }
    if (!exec.is_finished()) exec.shutdown();

    std::cout << "\n=== 1) host-tick + yield (absolute period) ===\n"
              << "  host sleep_until(period) then run(); task co_await yield()\n";
    maybe_slo(o, "host-tick+yield",
              bench::percentiles_from_ns(intervals),
              bench::percentiles_from_ns(late));
}

void run_sleep_for(const Options& o) {
    const ns period{std::chrono::microseconds(o.period_us)};
    const int total = ticks_for(o);
    std::vector<ns> intervals;
    std::vector<ns> late;
    intervals.reserve(static_cast<size_t>(total));
    late.reserve(static_cast<size_t>(total));

    rt::RtExecutor exec;
    if (o.pin_cpu >= 0) pin_current_thread_to_cpu(o.pin_cpu);

    const auto origin = steady::now();
    exec.spawn(sleep_for_worker(total, o.warmup, period, origin, intervals, late),
               "sleep_for");

    const auto wall = origin + std::chrono::milliseconds(o.duration_ms + 2000);
    while (!exec.is_finished() && steady::now() < wall) {
        exec.run();
        if (exec.is_finished()) break;
        // 当前 main 的 run_blocking 空闲仍盲睡 1ms；这里用 200us 轮询。
        // PR #20 的 next_timer_deadline / sleep_until 会更准 —— 不在本 PR 改调度器。
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (!exec.is_finished()) exec.shutdown();

    std::cout << "\n=== 2) sleep_for relative period (will drift) ===\n"
              << "  co_await sleep_for(period); host polls run() every ~200us\n"
              << "  tardiness vs absolute origin+i*period — relative sleep stacks error\n";
    maybe_slo(o, "sleep_for",
              bench::percentiles_from_ns(intervals),
              bench::percentiles_from_ns(late));
}

void run_sensor(const Options& o) {
    const ns period{std::chrono::microseconds(o.period_us)};
    const int total = ticks_for(o);
    std::vector<ns> intervals;
    std::vector<ns> late;
    intervals.reserve(static_cast<size_t>(total));
    late.reserve(static_cast<size_t>(total));

    std::atomic<void*> parked{nullptr};
    std::atomic<int64_t> fire_ns{0};
    std::atomic<bool> stop{false};

    rt::RtExecutor exec;
    if (o.pin_cpu >= 0) pin_current_thread_to_cpu(o.pin_cpu);
    exec.spawn(sensor_worker(total, o.warmup, parked, fire_ns, stop, intervals, late),
               "sensor");

    // 推进到第一次 park
    for (int i = 0; i < 10000 && parked.load(std::memory_order_acquire) == nullptr; ++i) {
        exec.run();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    const auto origin = steady::now();
    std::thread producer([&] {
        for (int i = 1; i <= total; ++i) {
            const auto fire = origin + period * i;
            std::this_thread::sleep_until(fire);
            fire_ns.store(std::chrono::duration_cast<ns>(fire.time_since_epoch()).count(),
                          std::memory_order_release);
            auto deadline_wait = steady::now() + std::chrono::milliseconds(200);
            void* addr = nullptr;
            while ((addr = parked.load(std::memory_order_acquire)) == nullptr) {
                if (steady::now() > deadline_wait) return;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
            parked.store(nullptr, std::memory_order_release);
            exec.post_ready(std::coroutine_handle<>::from_address(addr));
        }
        stop.store(true, std::memory_order_release);
    });

    const auto wall = origin + std::chrono::milliseconds(o.duration_ms + 2000);
    while (!exec.is_finished() && steady::now() < wall) {
        exec.run();  // 忙等 run：测 post_ready 到 resume 的迟到，不额外 sleep
        if (stop.load(std::memory_order_acquire) && parked.load() == nullptr
            && exec.is_finished()) break;
        if (stop.load(std::memory_order_acquire) && late.size() + static_cast<size_t>(o.warmup)
                >= static_cast<size_t>(total)) {
            exec.request_stop();
        }
    }
    producer.join();
    if (!exec.is_finished()) exec.shutdown();

    std::cout << "\n=== 3) cross-thread sensor post_ready ===\n"
              << "  producer thread sleep_until(period) then post_ready; executor busy run()\n"
              << "  tardiness = resume_now - intended fire time\n";
    maybe_slo(o, "sensor post_ready",
              bench::percentiles_from_ns(intervals),
              bench::percentiles_from_ns(late));
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        print_usage();
        return 2;
    }

    GlobalLogger::get().set_level(LogLevel::LOG_ERROR);

    std::cout
        << "FlowCoro RtExecutor cycle jitter (FlowEngine-style control loop)\n"
        << "  version=" << FLOWCORO_VERSION_STRING
#ifdef NDEBUG
        << "  build=Release\n"
#else
        << "  build=Debug\n"
#endif
        << "  this is wall-clock on this machine, not a hard-realtime certificate\n"
        << "  complements tests/test_rt_latency.cpp (PR #20) if present; does not\n"
        << "  change the scheduler. Uses sleep_for / yield / post_ready on current main.\n";

    run_host_tick(opt);
    run_sleep_for(opt);
    run_sensor(opt);

    std::cout
        << "\nHow to read:\n"
        << "  interval p50 should sit near the requested period.\n"
        << "  late p99/max is tardiness past the absolute deadline.\n"
        << "  sleep_for late grows with duration (relative sleep). host-tick should not.\n"
        << "  sensor path includes MPSC post_ready + busy run(); not zero-syscall.\n"
        << "  Reproduce: cmake --build build --target rt_cycle_jitter_benchmark\n"
        << "             FLOWCORO_RT_SLO_STRICT=1 ./build/benchmarks/rt_cycle_jitter_benchmark\n";
    return 0;
}
