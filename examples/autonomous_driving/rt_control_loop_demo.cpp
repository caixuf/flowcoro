/**
 * @file rt_control_loop_demo.cpp
 * @brief FlowEngine 式周期控制回路演示 — 只用 flowcoro::rt, 不依赖 DDS / ROS。
 *
 * 与 ad_pipeline_demo.cpp 的分工:
 *   - ad_pipeline_demo: 高吞吐 Task 调度 + 进程内 DDS Channel + when_any 超时降级
 *   - 本文件: 单线程 RtExecutor, sleep_until 对齐周期, 测 tick 抖动
 *
 * 模拟 50Hz 控制 + 30Hz 相机 + 10Hz LiDAR。传感器只把「最新一帧」写入原子快照,
 * 控制任务每拍读取, 不阻塞、不 mutex。这是确定性实时路径该有的形状, 不是完整 AD 栈。
 *
 * 构建与运行:
 *   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target rt_control_loop_demo
 *   ./build/examples/autonomous_driving/rt_control_loop_demo [秒数=2] [pin_cpu=-1]
 */

#include <flowcoro.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace flowcoro;
using namespace std::chrono_literals;

namespace {

using clock = std::chrono::steady_clock;

struct SensorSnap {
    std::atomic<int> camera_id{0};
    std::atomic<int> lidar_id{0};
    std::atomic<int64_t> camera_us{0};
    std::atomic<int64_t> lidar_us{0};
};

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
        .count();
}

rt::RtTask camera_task(SensorSnap& snap, int hz) {
    const auto period = std::chrono::microseconds(1'000'000 / hz);
    const auto origin = clock::now();
    int i = 0;
    while (!co_await rt::stop_requested()) {
        ++i;
        co_await rt::sleep_until(origin + period * i);
        snap.camera_id.store(i, std::memory_order_relaxed);
        snap.camera_us.store(now_us(), std::memory_order_relaxed);
    }
}

rt::RtTask lidar_task(SensorSnap& snap, int hz) {
    const auto period = std::chrono::microseconds(1'000'000 / hz);
    const auto origin = clock::now();
    int i = 0;
    while (!co_await rt::stop_requested()) {
        ++i;
        co_await rt::sleep_until(origin + period * i);
        snap.lidar_id.store(i, std::memory_order_relaxed);
        snap.lidar_us.store(now_us(), std::memory_order_relaxed);
    }
}

rt::RtTask control_task(SensorSnap& snap,
                        std::chrono::microseconds period,
                        std::vector<std::chrono::nanoseconds>& intervals,
                        std::vector<std::chrono::nanoseconds>& late,
                        std::atomic<int>& ticks) {
    const auto origin = clock::now();
    clock::time_point prev{};
    bool have_prev = false;
    int i = 0;
    while (!co_await rt::stop_requested()) {
        ++i;
        const auto deadline = origin + period * i;
        co_await rt::sleep_until(deadline);
        const auto now = clock::now();
        ticks.fetch_add(1, std::memory_order_relaxed);

        (void)snap.camera_id.load(std::memory_order_relaxed);
        (void)snap.lidar_id.load(std::memory_order_relaxed);

        auto tardiness = now - deadline;
        if (tardiness < clock::duration::zero()) tardiness = clock::duration::zero();
        late.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(tardiness));
        if (have_prev) {
            intervals.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(now - prev));
        }
        have_prev = true;
        prev = now;
    }
}

bool drive(rt::RtExecutor& exec, clock::time_point until) {
    while (clock::now() < until && !exec.is_finished()) {
        exec.run();
        if (exec.has_local_work()) continue;
        if (auto next = exec.next_timer_deadline()) {
            std::this_thread::sleep_until(*next);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    exec.request_stop();
    auto drain_by = clock::now() + 2s;
    while (!exec.is_finished() && clock::now() < drain_by) {
        exec.run();
        if (exec.has_local_work()) continue;
        if (auto next = exec.next_timer_deadline()) {
            std::this_thread::sleep_until(*next);
        }
    }
    if (!exec.is_finished()) exec.shutdown();
    return exec.is_finished();
}

void print_report(const char* tag, const rt::JitterReport& r) {
    std::cout << std::fixed << std::setprecision(3)
              << "  " << std::setw(12) << tag
              << "  n=" << r.n
              << "  p50=" << rt::ns_to_ms(r.p50) << "ms"
              << "  p99=" << rt::ns_to_ms(r.p99) << "ms"
              << "  max=" << rt::ns_to_ms(r.max) << "ms"
              << "  mean=" << rt::ns_to_ms(r.mean) << "ms\n";
}

} // namespace

int main(int argc, char** argv) {
    int run_seconds = 2;
    int pin_cpu = -1;
    if (argc >= 2) run_seconds = std::max(1, std::atoi(argv[1]));
    if (argc >= 3) pin_cpu = std::atoi(argv[2]);

    std::cout << "\nFlowCoro rt control-loop demo (no DDS)\n";
    std::cout << "  duration : " << run_seconds << " s\n";
    std::cout << "  control  : 50 Hz (sleep_until aligned)\n";
    std::cout << "  camera   : 30 Hz  |  lidar : 10 Hz  (latest-sample atomics)\n";
    std::cout << "  pin_cpu  : " << pin_cpu << "  (Linux only; -1 = no pin)\n\n";

    SensorSnap snap;
    std::vector<std::chrono::nanoseconds> intervals;
    std::vector<std::chrono::nanoseconds> late;
    std::atomic<int> ticks{0};
    intervals.reserve(static_cast<std::size_t>(run_seconds * 50 + 8));
    late.reserve(static_cast<std::size_t>(run_seconds * 50 + 8));

    rt::RtExecutor exec(rt::RtExecutor::Config{
        .pin_cpu = pin_cpu,
        .idle_sleep_us = 0,  // 本 demo 自己按 next_timer_deadline 等待
    });
    const bool pinned = exec.apply_affinity();
    std::cout << "  apply_affinity: " << (pinned ? "ok" : "skipped/failed") << "\n\n";

    exec.spawn(camera_task(snap, 30), "camera");
    exec.spawn(lidar_task(snap, 10), "lidar");
    exec.spawn(control_task(snap, 20ms, intervals, late, ticks), "control");

    const bool ok = drive(exec, clock::now() + std::chrono::seconds(run_seconds));

    const auto iv = rt::jitter_report(intervals);
    const auto lt = rt::jitter_report(late);

    std::cout << "  ticks=" << ticks.load()
              << "  camera=" << snap.camera_id.load()
              << "  lidar=" << snap.lidar_id.load()
              << "  finished=" << (ok ? "yes" : "NO") << "\n";
    print_report("interval", iv);
    print_report("late", lt);
    std::cout << "\n  这是本机这次跑的墙钟抖动, 不是产品级硬实时证书。\n"
              << "  更严闸门: FLOWCORO_RT_SLO_STRICT=1 ctest -R test_rt_latency\n\n";
    return ok ? 0 : 1;
}
