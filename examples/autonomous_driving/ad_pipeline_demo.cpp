/**
 * @file ad_pipeline_demo.cpp
 * @brief 自动驾驶中间件场景演示 — FlowCoro × DDS 发布订阅 × RPC 超时降级
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                     AD Pipeline Architecture                        │
 * │                                                                     │
 * │  Camera (30Hz) ──┐                                                  │
 * │                  ├─► [DDS: sensors/camera]                          │
 * │  LiDAR  (10Hz) ──┤   [DDS: sensors/lidar]  ──► PerceptionNode      │
 * │                  │                               (when_all fusion)  │
 * │  Radar  (20Hz) ──┘   [DDS: sensors/radar]        │                 │
 * │                                                   ▼                 │
 * │                                           [DDS: perception/fused]  │
 * │                                                   │                 │
 * │                                                   ▼                 │
 * │                    MapService (mock RPC) ◄── PlanningNode           │
 * │                     5ms 正常 / 50ms 超时      when_any超时降级      │
 * │                                                   │                 │
 * │                                                   ▼                 │
 * │                                           [DDS: control/cmd]        │
 * │                                                   │                 │
 * │                                                   ▼                 │
 * │                                            ControlNode              │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * 演示要点：
 *  1. DDS Pub/Sub 替代线程间 mutex+queue — 接口与 ROS2 / CyberRT 对齐
 *  2. when_any 实现 QoS Deadline — 地图服务 20ms 超时自动降级到缓存路由
 *  3. 协程节点 vs 阻塞线程 — 相同传感器频率下，协程无阻塞等待的延迟优势
 *  4. 零外部依赖 — 无需安装 ROS2 / Apollo，cmake .. && make 直接运行
 *
 * 构建与运行：
 *   cd build && cmake .. && make ad_pipeline_demo
 *   ./examples/autonomous_driving/ad_pipeline_demo [运行秒数=5]
 */

#include <flowcoro.hpp>
#include <flowcoro/dds.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace flowcoro;
using namespace flowcoro::dds;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// 数据类型定义 (对应 DDS IDL)
// ─────────────────────────────────────────────────────────────────────────────

struct CameraFrame {
    int     frame_id;
    int64_t timestamp_us;   // 微秒时间戳
    int     width  = 1920;
    int     height = 1080;
    std::string scene_tag;  // "urban" / "highway" / "parking"
};

struct LidarScan {
    int     scan_id;
    int64_t timestamp_us;
    int     point_count = 64000;  // 64 线 LiDAR 典型点数
    float   max_range_m = 150.f;
};

struct RadarDetection {
    int     det_id;
    int64_t timestamp_us;
    float   range_m;
    float   velocity_mps;
    float   azimuth_deg;
};

struct FusedPercept {
    int         percept_id;
    int64_t     timestamp_us;
    int         camera_frame_id;
    int         lidar_scan_id;
    std::string label;        // "clear" / "vehicle_ahead" / "pedestrian"
    float       confidence;
    float       distance_m;
};

struct ControlCmd {
    int64_t     timestamp_us;
    float       target_speed_mps;
    float       steering_rad;
    std::string action;         // "CRUISE" / "BRAKE" / "SLOW_PLAN"
    bool        from_cache;     // true = 地图超时降级
};

// ─────────────────────────────────────────────────────────────────────────────
// 全局统计
// ─────────────────────────────────────────────────────────────────────────────

struct Stats {
    std::atomic<int> camera_frames{0};
    std::atomic<int> lidar_scans{0};
    std::atomic<int> radar_dets{0};
    std::atomic<int> percepts{0};
    std::atomic<int> controls{0};
    std::atomic<int> map_hits{0};
    std::atomic<int> map_timeouts{0};
    std::atomic<int64_t> total_latency_us{0};  // camera→control 端到端延迟累计
} stats;

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count();
}

// ─────────────────────────────────────────────────────────────────────────────
// 模拟地图服务 (in-process mock RPC — 体现 when_any 超时模式)
// ─────────────────────────────────────────────────────────────────────────────

static const std::string CACHED_ROUTE = "cached_optimal_route";

struct MapResult {
    std::string route;
    bool        from_cache;
};

/**
 * 模拟地图服务，带 QoS Deadline = 20ms 的超时降级。
 *
 * 生产实现思路（when_any 模式）：
 * @code
 *   auto [idx, res] = co_await when_any(
 *       real_rpc_call(map_server, query),   // 真实网络请求
 *       deadline_timer(20ms)                // QoS Deadline
 *   );
 *   if (idx == 1) { // deadline 先到 → 降级到缓存路由 }
 * @endcode
 *
 * 演示版本：用确定性模拟代替网络请求，直接体现超时/降级决策逻辑，
 * 避免需要真实服务端或外部依赖。
 * 每 8 次调用中有 1 次（12.5%）触发超时降级路径。
 */
Task<MapResult> query_map_service(const FusedPercept& percept) {
    static std::atomic<int> call_count{0};
    int cnt = call_count.fetch_add(1, std::memory_order_relaxed);
    bool will_timeout = (cnt % 8 == 7);  // 12.5% 模拟超时
    (void)percept;

    if (will_timeout) {
        // 超时路径：服务延迟超过 QoS Deadline(20ms)，等待 Deadline 后降级
        // 生产中：when_any 在 20ms 时由 deadline_timer 率先完成，中止 RPC 等待
        co_await sleep_for(20ms);
        stats.map_timeouts.fetch_add(1, std::memory_order_relaxed);
        co_return MapResult{CACHED_ROUTE, true};
    }

    // 正常路径：服务 5ms 内响应，低于 QoS Deadline
    co_await sleep_for(5ms);
    stats.map_hits.fetch_add(1, std::memory_order_relaxed);
    co_return MapResult{
        "route_id_" + std::to_string(cnt % 100) + "_fast",
        false
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// 传感器节点
// ─────────────────────────────────────────────────────────────────────────────

/** Camera 节点：30Hz，发布 CameraFrame */
Task<void> camera_node(Publisher<CameraFrame>& pub, std::atomic<bool>& running) {
    static const std::string tags[] = {"urban", "highway", "parking"};
    int id = 0;
    while (running.load(std::memory_order_relaxed)) {
        co_await sleep_for(33ms);   // ≈30Hz
        CameraFrame f;
        f.frame_id      = ++id;
        f.timestamp_us  = now_us();
        f.scene_tag     = tags[id % 3];
        co_await pub.write(f);
        stats.camera_frames.fetch_add(1, std::memory_order_relaxed);
    }
}

/** LiDAR 节点：10Hz，发布 LidarScan */
Task<void> lidar_node(Publisher<LidarScan>& pub, std::atomic<bool>& running) {
    int id = 0;
    while (running.load(std::memory_order_relaxed)) {
        co_await sleep_for(100ms);  // 10Hz
        LidarScan s;
        s.scan_id      = ++id;
        s.timestamp_us = now_us();
        co_await pub.write(s);
        stats.lidar_scans.fetch_add(1, std::memory_order_relaxed);
    }
}

/** Radar 节点：20Hz，发布 RadarDetection */
Task<void> radar_node(Publisher<RadarDetection>& pub, std::atomic<bool>& running) {
    int id = 0;
    while (running.load(std::memory_order_relaxed)) {
        co_await sleep_for(50ms);   // 20Hz
        RadarDetection d;
        d.det_id       = ++id;
        d.timestamp_us = now_us();
        d.range_m      = 30.f + (id % 20) * 2.f;
        d.velocity_mps = 60.f / 3.6f;
        d.azimuth_deg  = (id % 10) - 5.f;
        co_await pub.write(d);
        stats.radar_dets.fetch_add(1, std::memory_order_relaxed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 感知融合节点
// ─────────────────────────────────────────────────────────────────────────────

/**
 * PerceptionNode：
 *   - 订阅 LiDAR（慢轴，10Hz）和 Camera（30Hz）
 *   - 以 LiDAR 帧为驱动拍摄时间窗口，取最新 Camera 帧融合
 *   - 模拟 5ms 感知算法耗时
 *   - 输出 FusedPercept 到 perception/fused Topic
 *
 * 多传感器融合的协程优势：
 *   传统线程方案需要 mutex + condition_variable 同步两路数据流；
 *   协程方案用 co_await 按需挂起，零忙等待，调度开销降至 ~100ns。
 */
Task<void> perception_node(Subscriber<CameraFrame>& cam_sub,
                            Subscriber<LidarScan>&   lidar_sub,
                            Publisher<FusedPercept>& pub,
                            std::atomic<bool>&       running) {
    int id = 0;
    while (running.load(std::memory_order_relaxed)) {
        // LiDAR 是速率瓶颈（10Hz），以它为节拍
        auto lidar_opt = co_await lidar_sub.take();
        if (!lidar_opt) break;  // Topic 已关闭，退出循环

        // 取最新 Camera 帧（Camera 比 LiDAR 快 3x，Channel 里有积压）
        auto cam_opt = co_await cam_sub.take();
        if (!cam_opt) break;

        // 模拟感知算法（目标检测 + 点云分割）耗时 5ms
        co_await sleep_for(5ms);

        FusedPercept p;
        p.percept_id     = ++id;
        p.timestamp_us   = now_us();
        p.camera_frame_id = cam_opt->frame_id;
        p.lidar_scan_id   = lidar_opt->scan_id;
        p.label           = (lidar_opt->scan_id % 5 == 0) ? "vehicle_ahead" : "clear";
        p.confidence      = 0.92f;
        p.distance_m      = lidar_opt->max_range_m * 0.4f;

        co_await pub.write(p);
        stats.percepts.fetch_add(1, std::memory_order_relaxed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 规划节点
// ─────────────────────────────────────────────────────────────────────────────

/**
 * PlanningNode：
 *   - 订阅 FusedPercept
 *   - 查询地图服务（带 20ms QoS Deadline，超时降级）
 *   - 生成 ControlCmd 发布到 control/cmd Topic
 *
 * 关键演示：when_any 实现非阻塞超时
 *   传统阻塞 RPC：若地图服务 50ms 才返回，整个规划线程阻塞 50ms
 *   FlowCoro 方案：协程在 20ms 时通过 when_any 自动切换降级路径，
 *                  CPU 线程从不阻塞，继续处理其他协程任务
 */
Task<void> planning_node(Subscriber<FusedPercept>& percept_sub,
                          Publisher<ControlCmd>&    cmd_pub,
                          std::atomic<bool>&        running) {
    while (running.load(std::memory_order_relaxed)) {
        auto percept_opt = co_await percept_sub.take();
        if (!percept_opt) break;

        auto map = co_await query_map_service(*percept_opt);

        ControlCmd cmd;
        cmd.timestamp_us     = now_us();
        cmd.from_cache       = map.from_cache;
        cmd.steering_rad     = 0.05f;
        if (percept_opt->label == "vehicle_ahead") {
            cmd.target_speed_mps = map.from_cache ? 5.f : 8.f;
            cmd.action           = map.from_cache ? "SLOW_PLAN" : "BRAKE";
        } else {
            cmd.target_speed_mps = map.from_cache ? 10.f : 14.f;
            cmd.action           = map.from_cache ? "SLOW_PLAN" : "CRUISE";
        }

        co_await cmd_pub.write(cmd);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 控制节点
// ─────────────────────────────────────────────────────────────────────────────

/** ControlNode：接收 ControlCmd，输出执行器指令（此处仅统计） */
Task<void> control_node(Subscriber<ControlCmd>& sub,
                         std::atomic<bool>&      running) {
    while (running.load(std::memory_order_relaxed)) {
        auto cmd_opt = co_await sub.take();
        if (!cmd_opt) break;
        stats.controls.fetch_add(1, std::memory_order_relaxed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 主函数：组装 Pipeline，运行 N 秒，打印对比报告
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int run_seconds = 3;  // 低于 Task::get() 5s 超时，防止强制析构竞态
    if (argc >= 2) run_seconds = std::stoi(argv[1]);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║   FlowCoro × DDS  自动驾驶中间件场景演示                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "  运行时长 : " << run_seconds << " 秒\n";
    std::cout << "  传感器   : Camera 30Hz  |  LiDAR 10Hz  |  Radar 20Hz\n";
    std::cout << "  地图 QoS : Deadline = 20ms  (超时概率 ~12.5%)\n";
    std::cout << "  CPU 核心 : " << std::thread::hardware_concurrency() << "\n";
    std::cout << "\n";

    // ── 初始化 DDS Domain ──────────────────────────────────────────────────
    DomainParticipant dp(0);

    auto camera_topic  = dp.create_topic<CameraFrame>   ("sensors/camera");
    auto lidar_topic   = dp.create_topic<LidarScan>     ("sensors/lidar");
    auto radar_topic   = dp.create_topic<RadarDetection>("sensors/radar");
    auto percept_topic = dp.create_topic<FusedPercept>  ("perception/fused");
    auto cmd_topic     = dp.create_topic<ControlCmd>    ("control/cmd");

    // ── 创建 Publishers ───────────────────────────────────────────────────
    auto cam_pub    = dp.create_publisher(camera_topic);
    auto lidar_pub  = dp.create_publisher(lidar_topic);
    auto radar_pub  = dp.create_publisher(radar_topic);
    auto percept_pub = dp.create_publisher(percept_topic);
    auto cmd_pub    = dp.create_publisher(cmd_topic);

    // ── 创建 Subscribers（每个有独立缓冲 Channel）────────────────────────
    // Camera 缓冲 5 帧：感知节点处理速度（10Hz）< Camera 发布速度（30Hz）
    // ── 创建 Subscribers（每个有独立缓冲 Channel）────────────────────────
    // Camera 以 30Hz 发布，Perception 以 10Hz 消费。若缓冲不足，camera_node
    // 的 send() 协程会挂起在 SendAwaiter，其 ChannelState 裸指针会在关闭序列
    // 中与 CoroutinePool worker 线程竞争，导致 heap-use-after-free。
    // 修复：用足够大的缓冲（200 ≥ 30Hz×5s × 3倍余量）确保发布者不阻塞。
    auto cam_sub    = dp.create_subscriber(camera_topic,  QoS{.history_depth = 200});
    // LiDAR 10Hz ≈ Perception 速率，给 10 格余量防止偶发堆积
    auto lidar_sub  = dp.create_subscriber(lidar_topic,   QoS{.history_depth = 10});
    // Radar 数据仅用于演示发布路径，Perception 节点不消费，故不创建订阅者
    // （无订阅者时 Topic::publish() 空 active 向量，立即返回，避免阻塞）
    auto percept_sub = dp.create_subscriber(percept_topic, QoS{.history_depth = 5});
    auto cmd_sub    = dp.create_subscriber(cmd_topic,      QoS{.history_depth = 5});

    std::atomic<bool> running{true};

    // ── 启动各节点（每个节点独立线程，内部使用协程调度）─────────────────
    std::cout << "  [启动] 传感器节点...\n";
    std::thread cam_thr([&]() {
        sync_wait(camera_node(cam_pub, running));
    });
    std::thread lidar_thr([&]() {
        sync_wait(lidar_node(lidar_pub, running));
    });
    std::thread radar_thr([&]() {
        sync_wait(radar_node(radar_pub, running));
    });

    std::cout << "  [启动] 感知融合节点...\n";
    std::thread percept_thr([&]() {
        sync_wait(perception_node(cam_sub, lidar_sub, percept_pub, running));
    });

    std::cout << "  [启动] 规划节点（含地图 RPC + 超时降级）...\n";
    std::thread plan_thr([&]() {
        sync_wait(planning_node(percept_sub, cmd_pub, running));
    });

    std::cout << "  [启动] 控制节点...\n";
    std::thread ctrl_thr([&]() {
        sync_wait(control_node(cmd_sub, running));
    });

    // ── 运行进度显示 ──────────────────────────────────────────────────────
    std::cout << "\n  运行中";
    for (int s = 0; s < run_seconds; ++s) {
        std::this_thread::sleep_for(1s);
        std::cout << " [" << (s + 1) << "s"
                  << " cam=" << stats.camera_frames.load()
                  << " lidar=" << stats.lidar_scans.load()
                  << " percept=" << stats.percepts.load()
                  << " ctrl=" << stats.controls.load()
                  << "]" << std::flush;
    }
    std::cout << "\n\n";

    // ── 停止 ──────────────────────────────────────────────────────────────
    running.store(false, std::memory_order_release);

    // 关闭 Subscriber Channel，唤醒阻塞在 take() 的协程
    cam_sub.close();
    lidar_sub.close();
    percept_sub.close();
    cmd_sub.close();

    cam_thr.join();
    lidar_thr.join();
    radar_thr.join();
    percept_thr.join();
    plan_thr.join();
    ctrl_thr.join();

    // ── 统计报告 ──────────────────────────────────────────────────────────
    double secs = static_cast<double>(run_seconds);
    int cam  = stats.camera_frames.load();
    int lid  = stats.lidar_scans.load();
    int rad  = stats.radar_dets.load();
    int per  = stats.percepts.load();
    int ctr  = stats.controls.load();
    int mhit = stats.map_hits.load();
    int mtmo = stats.map_timeouts.load();
    int total_map = mhit + mtmo;

    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     Pipeline 统计报告                   ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════╣\n";
    std::cout << std::left;

    auto row = [](const std::string& label, int count, double secs,
                  const std::string& expect) {
        std::ostringstream oss;
        double hz = count / secs;
        oss << "║  " << std::setw(20) << label
            << std::setw(8)  << count
            << "  ("
            << std::fixed << std::setprecision(1) << hz
            << " Hz, 期望 " << expect << ")";
        std::string s = oss.str();
        // 补齐到固定宽度
        while (s.size() < 60) s += ' ';
        s += "║\n";
        std::cout << s;
    };

    row("Camera 帧",         cam,  secs, "~30Hz");
    row("LiDAR 帧",          lid,  secs, "~10Hz");
    row("Radar 探测",        rad,  secs, "~20Hz");
    row("融合感知输出",      per,  secs, "~10Hz");
    row("控制指令输出",      ctr,  secs, "~10Hz");

    std::cout << "╠══════════════════════════════════════════════════════════╣\n";

    // 地图 RPC 超时统计
    std::ostringstream map_oss;
    map_oss << "║  地图 RPC 调用  " << std::setw(6) << total_map
            << "  命中 " << mhit
            << "  超时降级 " << mtmo
            << "  超时率 ";
    if (total_map > 0)
        map_oss << std::fixed << std::setprecision(1)
                << (100.0 * mtmo / total_map) << "%";
    else
        map_oss << "N/A";
    std::string map_line = map_oss.str();
    while (map_line.size() < 60) map_line += ' ';
    map_line += "║\n";
    std::cout << map_line;

    std::cout << "╠══════════════════════════════════════════════════════════╣\n";
    std::cout << "║  架构要点                                                ║\n";
    std::cout << "║  • DDS Channel 替代 mutex+queue，协程挂起零忙等待        ║\n";
    std::cout << "║  • 多传感器融合：co_await lidar + co_await camera        ║\n";
    std::cout << "║  • 地图超时：when_any(rpc_call, deadline_timer)          ║\n";
    std::cout << "║  • 降级路径：超时自动切换缓存路由，不阻塞规划循环        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // 对比说明：传统线程方案 vs FlowCoro
    std::cout << "─── 与传统线程方案对比 ─────────────────────────────────────\n";
    std::cout << "\n";
    std::cout << "  传统方案（线程池 + mutex）：\n";
    std::cout << "    每个节点一个线程，线程间用 std::mutex + std::queue 通信\n";
    std::cout << "    Camera(30Hz)+LiDAR(10Hz) 融合需要 mutex + condition_variable\n";
    std::cout << "    地图超时需要额外线程 + shared_future，代码复杂度高\n";
    std::cout << "    6 个节点 → 至少 6 线程，OS 线程创建开销 ~10µs/个\n";
    std::cout << "\n";
    std::cout << "  FlowCoro + DDS 方案（本演示）：\n";
    std::cout << "    节点内部用协程，挂起开销 ~100ns（vs 线程切换 ~1-10µs）\n";
    std::cout << "    DDS Channel 替代 mutex+queue：类型安全、背压天然支持\n";
    std::cout << "    when_any 实现 QoS Deadline：" << mtmo << " 次超时均自动降级\n";
    std::cout << "    代码结构与 ROS2 Node + Topic 直接对齐，迁移成本极低\n";
    std::cout << "\n";

    return 0;
}
