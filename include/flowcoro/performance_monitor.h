#pragma once
#include <atomic>
#include <chrono>
#include <iostream>

namespace flowcoro {

// 统计信息结构
struct SystemStats {
    uint64_t tasks_created;
    uint64_t tasks_completed;
    uint64_t tasks_cancelled;
    uint64_t tasks_failed;
    uint64_t scheduler_invocations;
    uint64_t timer_events;
    uint64_t uptime_ms;
    double task_completion_rate;
    double tasks_per_second;
};

// 性能监控系统
class PerformanceMonitor {
private:
    struct Stats {
        std::atomic<uint64_t> tasks_created{0};
        std::atomic<uint64_t> tasks_completed{0};
        std::atomic<uint64_t> tasks_cancelled{0};
        std::atomic<uint64_t> tasks_failed{0};
        std::atomic<uint64_t> scheduler_invocations{0};
        std::atomic<uint64_t> timer_events{0};
        std::chrono::steady_clock::time_point start_time;
    };
    
    Stats stats_;
    
public:
    PerformanceMonitor() {
        stats_.start_time = std::chrono::steady_clock::now();
    }
    
    // 统计接口
    void on_task_created() noexcept { stats_.tasks_created.fetch_add(1, std::memory_order_relaxed); }
    void on_task_completed() noexcept { stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed); }
    void on_task_cancelled() noexcept { stats_.tasks_cancelled.fetch_add(1, std::memory_order_relaxed); }
    void on_task_failed() noexcept { stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed); }
    void on_scheduler_invocation() noexcept { stats_.scheduler_invocations.fetch_add(1, std::memory_order_relaxed); }
    void on_timer_event() noexcept { stats_.timer_events.fetch_add(1, std::memory_order_relaxed); }
    
    // 获取统计信息
    SystemStats get_stats() const noexcept {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_.start_time);
        
        uint64_t created = stats_.tasks_created.load(std::memory_order_relaxed);
        uint64_t completed = stats_.tasks_completed.load(std::memory_order_relaxed);
        
        return SystemStats{
            .tasks_created = created,
            .tasks_completed = completed,
            .tasks_cancelled = stats_.tasks_cancelled.load(std::memory_order_relaxed),
            .tasks_failed = stats_.tasks_failed.load(std::memory_order_relaxed),
            .scheduler_invocations = stats_.scheduler_invocations.load(std::memory_order_relaxed),
            .timer_events = stats_.timer_events.load(std::memory_order_relaxed),
            .uptime_ms = static_cast<uint64_t>(uptime.count()),
            .task_completion_rate = created > 0 ? (double)completed / created : 0.0,
            .tasks_per_second = uptime.count() > 0 ? (double)completed * 1000.0 / uptime.count() : 0.0
        };
    }
    
    void print_stats() const {
        auto stats = get_stats();
        std::cout << "\n=== FlowCoro Performance Statistics ===\n";
        std::cout << "Uptime: " << stats.uptime_ms << " ms\n";
        std::cout << "Tasks Created: " << stats.tasks_created << "\n";
        std::cout << "Tasks Completed: " << stats.tasks_completed << "\n";
        std::cout << "Tasks Cancelled: " << stats.tasks_cancelled << "\n";
        std::cout << "Tasks Failed: " << stats.tasks_failed << "\n";
        std::cout << "Scheduler Invocations: " << stats.scheduler_invocations << "\n";
        std::cout << "Timer Events: " << stats.timer_events << "\n";
        std::cout << "Completion Rate: " << (stats.task_completion_rate * 100.0) << "%\n";
        std::cout << "Throughput: " << stats.tasks_per_second << " tasks/sec\n";
        std::cout << "========================================\n";
    }
    
    static PerformanceMonitor& get_instance() {
        static PerformanceMonitor instance;
        return instance;
    }
};

} // namespace flowcoro
