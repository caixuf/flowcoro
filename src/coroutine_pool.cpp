#include "flowcoro/core.h"
#include "flowcoro/thread_pool.h"
#include "flowcoro/lockfree.h"
#include <iostream>
#include <iomanip>
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <mutex>

namespace flowcoro {

// ==========================================
// 单个协程调度器 - 每个调度器独立运行在自己的线程中
// ==========================================

class CoroutineScheduler {
private:
    size_t scheduler_id_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    
    // 无锁协程队列 - 高性能无锁实现
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    // 统计信息
    std::atomic<size_t> total_coroutines_{0};
    std::atomic<size_t> completed_coroutines_{0};
    std::chrono::steady_clock::time_point start_time_;

    void worker_loop() {
        const size_t BATCH_SIZE = 128; // 增加批处理大小，进一步减少竞争
        std::vector<std::coroutine_handle<>> batch;
        batch.reserve(BATCH_SIZE);
        
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        
        while (!stop_flag_.load()) {
            batch.clear();
            
            // 批量提取协程 - 使用无锁队列
            std::coroutine_handle<> handle;
            while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
            }
            
            // 如果没有任务，短暂休眠
            if (batch.empty()) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            
            // 批量执行协程 - 移除异常处理以提高性能
            for (auto handle : batch) {
                if (handle && !handle.done() && !stop_flag_.load()) {
                    handle.resume();
                    completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

public:
    CoroutineScheduler(size_t id) 
        : scheduler_id_(id), start_time_(std::chrono::steady_clock::now()) {
        worker_thread_ = std::thread(&CoroutineScheduler::worker_loop, this);
    }
    
    ~CoroutineScheduler() {
        stop();
    }
    
    void stop() {
        if (!stop_flag_.exchange(true)) {
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            
            // 清理剩余协程 - 使用无锁队列
            std::coroutine_handle<> handle;
            while (coroutine_queue_.dequeue(handle)) {
                if (handle && !handle.done()) {
                    handle.destroy();
                }
            }
        }
    }
    
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;
        
        total_coroutines_.fetch_add(1, std::memory_order_relaxed);
        
        // 使用无锁队列添加协程
        coroutine_queue_.enqueue(handle);
        
        // 更新负载均衡器的队列大小信息（估算值）
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.update_load(scheduler_id_, 1); // 简化的负载更新
    }
    
    size_t get_queue_size() const {
        // 无锁队列不支持直接获取size，返回估算值
        return 0; // 或者维护一个原子计数器
    }
    
    size_t get_total_coroutines() const { return total_coroutines_.load(); }
    size_t get_completed_coroutines() const { return completed_coroutines_.load(); }
    size_t get_scheduler_id() const { return scheduler_id_; }
    
    std::chrono::milliseconds get_uptime() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    }
};

// ==========================================
// 多调度器协程池 - 管理12个独立的协程调度器
// ==========================================

class CoroutinePool {
private:
    static std::atomic<CoroutinePool*> instance_;
    
    // 动态根据CPU核心数确定调度器数量
    const size_t NUM_SCHEDULERS;
    
    // 动态数量的独立协程调度器
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 后台线程池 - 处理CPU密集型任务
    std::unique_ptr<lockfree::ThreadPool> thread_pool_;
    
    std::atomic<bool> stop_flag_{false};
    
    // 统计信息
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    CoroutinePool()
        : NUM_SCHEDULERS(std::max(std::thread::hardware_concurrency(), static_cast<unsigned int>(4))), // 调度器数=CPU核数，最少4个
          start_time_(std::chrono::steady_clock::now()) {
        // 根据CPU核心数初始化调度器
        schedulers_.reserve(NUM_SCHEDULERS);
        for (size_t i = 0; i < NUM_SCHEDULERS; ++i) {
            schedulers_.emplace_back(std::make_unique<CoroutineScheduler>(i));
        }
        
        // 初始化智能负载均衡器
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.set_scheduler_count(NUM_SCHEDULERS);
        
        // 智能线程池配置：大规模并发优化
        size_t thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 8; // 备用值

        // 高性能配置：工作线程数应该足够大以支持大规模并发
        thread_count = std::max(thread_count * 2, static_cast<size_t>(32)); // 至少32个线程，通常为核数的2倍
        thread_count = std::min(thread_count, static_cast<size_t>(128)); // 最多128个线程，避免过度创建

        thread_pool_ = std::make_unique<lockfree::ThreadPool>(thread_count);

        // 仅在需要时输出启动信息
        static bool first_init = true;
        if (first_init) {
            std::cout << "FlowCoro智能协程池启动 - " << NUM_SCHEDULERS 
                      << "个协程调度器 + " << thread_count 
                      << "个工作线程 (智能负载均衡)" << std::endl;
            first_init = false;
        }
    }

    ~CoroutinePool() {
        stop_flag_.store(true);
        
        // 停止所有调度器
        for (auto& scheduler : schedulers_) {
            if (scheduler) {
                scheduler->stop();
            }
        }
        
        schedulers_.clear();
        std::cout << "FlowCoro自适应协程池关闭 (" << NUM_SCHEDULERS << "个调度器)" << std::endl;
    }

    static CoroutinePool& get_instance() {
        CoroutinePool* current = instance_.load(std::memory_order_acquire);
        if (current == nullptr) {
            static std::mutex init_mutex;
            std::lock_guard<std::mutex> lock(init_mutex);
            current = instance_.load(std::memory_order_relaxed);
            if (current == nullptr) {
                current = new CoroutinePool();
                instance_.store(current, std::memory_order_release);
            }
        }
        return *current;
    }

    static void shutdown() {
        CoroutinePool* current = instance_.exchange(nullptr, std::memory_order_acq_rel);
        if (current) {
            delete current;
        }
    }

    // 协程调度 - 使用智能负载均衡分配到调度器
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;

        // 使用智能负载均衡：选择最优调度器
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        
        size_t scheduler_index = load_balancer.select_scheduler();
        
        // 增加选中调度器的负载计数
        load_balancer.increment_load(scheduler_index);
        
        // 分配给对应的调度器
        schedulers_[scheduler_index]->schedule_coroutine(handle);
    }

    // CPU密集型任务 - 提交到后台线程池 (优化版：无异常处理)
    void schedule_task(std::function<void()> task) {
        if (stop_flag_.load()) return;

        total_tasks_.fetch_add(1, std::memory_order_relaxed);

        // 将任务提交到后台线程池执行 - 移除异常处理提高性能
        thread_pool_->enqueue([this, task = std::move(task)]() {
            task(); // 直接执行，不捕获异常
            completed_tasks_.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 驱动协程池 - 现在由各个调度器自动运行，无需手动驱动
    void drive() {
        // 多调度器架构下，每个调度器都在独立线程中自动运行
        // 这个方法保留为兼容接口，实际不需要调用
        if (stop_flag_.load()) return;
        
        // 可以在这里添加一些全局监控逻辑
        // 但协程调度已经由各个独立调度器自动处理
    }

    // 获取统计信息
    struct PoolStats {
        size_t num_schedulers;
        size_t thread_pool_workers;
        size_t pending_coroutines;
        size_t total_coroutines;
        size_t completed_coroutines;
        size_t total_tasks;
        size_t completed_tasks;
        double coroutine_completion_rate;
        double task_completion_rate;
        std::chrono::milliseconds uptime;
    };

    PoolStats get_stats() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

        size_t pending = 0;
        size_t total_cor = 0;
        size_t completed_cor = 0;
        
        // 汇总所有调度器的统计信息
        for (const auto& scheduler : schedulers_) {
            if (scheduler) {
                pending += scheduler->get_queue_size();
                total_cor += scheduler->get_total_coroutines();
                completed_cor += scheduler->get_completed_coroutines();
            }
        }

        size_t total_task = total_tasks_.load();
        size_t completed_task = completed_tasks_.load();

        return PoolStats{
            NUM_SCHEDULERS, // num_schedulers
            std::max(std::thread::hardware_concurrency(), static_cast<unsigned int>(32)), // thread_pool_workers (显示实际线程数)
            pending, // pending_coroutines
            total_cor, // total_coroutines
            completed_cor, // completed_coroutines
            total_task, // total_tasks
            completed_task, // completed_tasks
            total_cor > 0 ? (double)completed_cor / total_cor : 0.0, // coroutine_completion_rate
            total_task > 0 ? (double)completed_task / total_task : 0.0, // task_completion_rate
            uptime
        };
    }

    void print_stats() const {
        auto stats = get_stats();

        std::cout << "\n=== FlowCoro 多调度器协程池统计 ===" << std::endl;
        std::cout << " 运行时间: " << stats.uptime.count() << " ms" << std::endl;
        std::cout << " 架构模式: " << stats.num_schedulers << "个独立协程调度器 + 后台线程池" << std::endl;
        std::cout << " 工作线程: " << stats.thread_pool_workers << " 个" << std::endl;
        std::cout << " 待处理协程: " << stats.pending_coroutines << std::endl;
        std::cout << " 总协程数: " << stats.total_coroutines << std::endl;
        std::cout << " 完成协程: " << stats.completed_coroutines << std::endl;
        std::cout << " 总任务数: " << stats.total_tasks << std::endl;
        std::cout << " 完成任务: " << stats.completed_tasks << std::endl;
        std::cout << " 协程完成率: " << std::fixed << std::setprecision(1)
                  << (stats.coroutine_completion_rate * 100) << "%" << std::endl;
        std::cout << " 任务完成率: " << std::fixed << std::setprecision(1)
                  << (stats.task_completion_rate * 100) << "%" << std::endl;
        
        // 显示各个调度器的详细信息
        std::cout << "\n--- 调度器详情 ---" << std::endl;
        for (const auto& scheduler : schedulers_) {
            if (scheduler) {
                std::cout << "调度器 #" << scheduler->get_scheduler_id() 
                          << " - 队列: " << scheduler->get_queue_size()
                          << ", 总数: " << scheduler->get_total_coroutines()
                          << ", 完成: " << scheduler->get_completed_coroutines()
                          << ", 运行时间: " << scheduler->get_uptime().count() << "ms" << std::endl;
            }
        }
        std::cout << "===============================" << std::endl;
    }
};

// 静态成员定义
std::atomic<CoroutinePool*> CoroutinePool::instance_{nullptr};

// ==========================================
// 全局接口函数 - 协程池驱动接口
// ==========================================

// 协程调度接口
void schedule_coroutine_enhanced(std::coroutine_handle<> handle) {
    CoroutinePool::get_instance().schedule_coroutine(handle);
}

// 任务调度接口 (提交到线程池)
void schedule_task_enhanced(std::function<void()> task) {
    CoroutinePool::get_instance().schedule_task(std::move(task));
}

// 驱动协程池 - 需要在主线程中定期调用
void drive_coroutine_pool() {
    CoroutinePool::get_instance().drive();
}

// 统计信息接口
void print_pool_stats() {
    CoroutinePool::get_instance().print_stats();
}

// 关闭接口
void shutdown_coroutine_pool() {
    CoroutinePool::shutdown();
}

// 运行协程直到完成的高性能实现 - 移除异常处理
void run_until_complete(Task<void>& task) {
    std::atomic<bool> completed{false};

    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();

    // 创建完成回调 - 简化版本，无异常处理
    auto completion_task = [&]() -> Task<void> {
        co_await task;
        completed.store(true, std::memory_order_release);
    }();

    // 启动任务
    manager.schedule_resume(completion_task.handle);

    // 等待完成并持续驱动协程管理器 - 优化轮询间隔
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 驱动协程调度
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // 减少睡眠时间提高响应性
    }

    // 最终清理
    manager.drive();
}

template<typename T>
void run_until_complete(Task<T>& task) {
    std::atomic<bool> completed{false};

    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();

    // 创建完成回调 - 简化版本，无异常处理
    auto completion_task = [&]() -> Task<void> {
        co_await task;
        completed.store(true, std::memory_order_release);
    }();

    // 启动任务
    manager.schedule_resume(completion_task.handle);

    // 等待完成并持续驱动协程管理器 - 优化轮询间隔
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 驱动协程调度
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // 减少睡眠时间提高响应性
    }

    // 最终清理
    manager.drive();
}

// 显式实例化常用类型
template void run_until_complete<int>(Task<int>&);
template void run_until_complete<void>(Task<void>&);

} // namespace flowcoro
