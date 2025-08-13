#pragma once
#include <atomic>
#include <array>
#include <vector>

namespace flowcoro {

// 智能负载均衡器 - 无锁实现
class SmartLoadBalancer {
private:
    static constexpr size_t MAX_SCHEDULERS = 32;
    std::array<std::atomic<size_t>, MAX_SCHEDULERS> queue_loads_;
    std::atomic<size_t> scheduler_count_{0};
    std::atomic<size_t> round_robin_counter_{0};

public:
    SmartLoadBalancer() {
        for (auto& load : queue_loads_) {
            load.store(0, std::memory_order_relaxed);
        }
    }

    void set_scheduler_count(size_t count) noexcept {
        scheduler_count_.store(std::min(count, MAX_SCHEDULERS), std::memory_order_release);
    }

    // 选择负载最小的调度器
    size_t select_scheduler() noexcept {
        size_t count = scheduler_count_.load(std::memory_order_acquire);
        if (count == 0) return 0;
        if (count == 1) return 0;

        // 快速路径：使用轮询（性能更好）
        size_t quick_choice = round_robin_counter_.fetch_add(1, std::memory_order_relaxed) % count;
        
        // 每16次执行一次负载检查
        if ((quick_choice & 0xF) == 0) {
            size_t min_load = SIZE_MAX;
            size_t best_scheduler = 0;
            
            for (size_t i = 0; i < count; ++i) {
                size_t load = queue_loads_[i].load(std::memory_order_relaxed);
                if (load < min_load) {
                    min_load = load;
                    best_scheduler = i;
                }
            }
            return best_scheduler;
        }
        
        return quick_choice;
    }

    // 更新调度器负载
    void update_load(size_t scheduler_id, size_t load) noexcept {
        if (scheduler_id < MAX_SCHEDULERS) {
            queue_loads_[scheduler_id].store(load, std::memory_order_relaxed);
        }
    }

    // 增加调度器负载
    void increment_load(size_t scheduler_id) noexcept {
        if (scheduler_id < MAX_SCHEDULERS) {
            queue_loads_[scheduler_id].fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 减少调度器负载
    void decrement_load(size_t scheduler_id) noexcept {
        if (scheduler_id < MAX_SCHEDULERS) {
            queue_loads_[scheduler_id].fetch_sub(1, std::memory_order_relaxed);
        }
    }
    
    // 记录任务完成
    void on_task_completed(size_t scheduler_id) noexcept {
        decrement_load(scheduler_id);
    }
    
    // 获取负载统计
    struct LoadStats {
        size_t scheduler_id;
        size_t queue_load;
        size_t total_processed;
        double load_score;
    };
    
    std::vector<LoadStats> get_load_stats() const {
        std::vector<LoadStats> stats;
        size_t count = scheduler_count_.load(std::memory_order_acquire);
        
        for (size_t i = 0; i < count; ++i) {
            size_t queue_load = queue_loads_[i].load(std::memory_order_relaxed);
            stats.push_back({
                .scheduler_id = i,
                .queue_load = queue_load,
                .total_processed = 0, // 简化实现，暂不跟踪处理总数
                .load_score = static_cast<double>(queue_load)
            });
        }
        
        return stats;
    }
};

} // namespace flowcoro
