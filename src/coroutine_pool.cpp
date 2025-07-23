#include "../include/flowcoro/core.h"
#include <iostream>
#include <iomanip>

namespace flowcoro {

// ==========================================
// 简单的协程池实现
// ==========================================

class SimpleCoroutinePool {
private:
    static SimpleCoroutinePool* instance_;
    static std::mutex instance_mutex_;
    
    std::vector<std::thread> worker_threads_;
    std::queue<std::function<void()>> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_flag_{false};
    
    // 统计信息
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<size_t> active_workers_{0};
    std::chrono::steady_clock::time_point start_time_;
    
public:
    SimpleCoroutinePool(size_t num_threads = std::thread::hardware_concurrency()) 
        : start_time_(std::chrono::steady_clock::now()) {
        
        // 启动工作线程
        for (size_t i = 0; i < num_threads; ++i) {
            worker_threads_.emplace_back([this] {
                worker_loop();
            });
        }
    }
    
    ~SimpleCoroutinePool() {
        stop_flag_.store(true);
        condition_.notify_all();
        
        for (auto& worker : worker_threads_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    static SimpleCoroutinePool& get_instance() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (!instance_) {
            instance_ = new SimpleCoroutinePool();
        }
        return *instance_;
    }
    
    static void shutdown() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        delete instance_;
        instance_ = nullptr;
    }
    
    // 调度协程任务
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done()) return;
        
        schedule_task([handle]() {
            if (handle && !handle.done()) {
                handle.resume();
            }
        });
    }
    
    // 调度一般任务
    void schedule_task(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_flag_.load()) return;
            
            task_queue_.push(std::move(task));
            total_tasks_.fetch_add(1);
        }
        condition_.notify_one();
    }
    
    // 获取统计信息
    struct PoolStats {
        size_t worker_count;
        size_t active_workers;
        size_t pending_tasks;
        size_t total_tasks;
        size_t completed_tasks;
        double utilization_rate;
        std::chrono::milliseconds uptime;
    };
    
    PoolStats get_stats() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        auto uptime = std::chrono::steady_clock::now() - start_time_;
        auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(uptime);
        
        PoolStats stats{};
        stats.worker_count = worker_threads_.size();
        stats.active_workers = active_workers_.load();
        stats.pending_tasks = task_queue_.size();
        stats.total_tasks = total_tasks_.load();
        stats.completed_tasks = completed_tasks_.load();
        stats.utilization_rate = stats.worker_count > 0 ? 
            (double)stats.active_workers / stats.worker_count : 0.0;
        stats.uptime = uptime_ms;
        
        return stats;
    }
    
    void print_stats() const {
        auto stats = get_stats();
        
        std::cout << "=== FlowCoro 协程池统计 ===" << std::endl;
        std::cout << "运行时间: " << stats.uptime.count() << " ms" << std::endl;
        std::cout << "工作线程: " << stats.worker_count << std::endl;
        std::cout << "活跃线程: " << stats.active_workers << std::endl;
        std::cout << "待处理任务: " << stats.pending_tasks << std::endl;
        std::cout << "总任务数: " << stats.total_tasks << std::endl;
        std::cout << "完成任务: " << stats.completed_tasks << std::endl;
        std::cout << "线程利用率: " << std::fixed << std::setprecision(1) 
                  << (stats.utilization_rate * 100) << "%" << std::endl;
        std::cout << "===============================" << std::endl;
    }
    
private:
    void worker_loop() {
        while (!stop_flag_.load()) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] {
                    return stop_flag_.load() || !task_queue_.empty();
                });
                
                if (stop_flag_.load()) break;
                
                if (!task_queue_.empty()) {
                    task = std::move(task_queue_.front());
                    task_queue_.pop();
                }
            }
            
            if (task) {
                active_workers_.fetch_add(1);
                try {
                    task();
                    completed_tasks_.fetch_add(1);
                } catch (...) {
                    // 忽略任务异常
                }
                active_workers_.fetch_sub(1);
            }
        }
    }
};

// 静态成员定义
SimpleCoroutinePool* SimpleCoroutinePool::instance_ = nullptr;
std::mutex SimpleCoroutinePool::instance_mutex_;

// ==========================================
// 全局接口函数
// ==========================================

// 协程调度接口
void schedule_coroutine_enhanced(std::coroutine_handle<> handle) {
    SimpleCoroutinePool::get_instance().schedule_coroutine(handle);
}

// 任务调度接口
void schedule_task_enhanced(std::function<void()> task) {
    SimpleCoroutinePool::get_instance().schedule_task(std::move(task));
}

// 统计信息接口
void print_pool_stats() {
    SimpleCoroutinePool::get_instance().print_stats();
}

// 关闭接口
void shutdown_coroutine_pool() {
    SimpleCoroutinePool::shutdown();
}

// 运行协程直到完成的安全实现
void run_until_complete(Task<void>& task) {
    std::atomic<bool> completed{false};
    std::exception_ptr exception_holder = nullptr;
    
    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();
    
    // 创建完成回调
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true);
        } catch (...) {
            exception_holder = std::current_exception();
            completed.store(true);
        }
    }();
    
    // 启动任务
    manager.schedule_resume(completion_task.handle);
    
    // 等待完成并持续驱动协程管理器
    while (!completed.load()) {
        manager.drive();  // 驱动协程调度
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 最终清理
    manager.drive();
    
    // 重新抛出异常
    if (exception_holder) {
        std::rethrow_exception(exception_holder);
    }
}

template<typename T>
void run_until_complete(Task<T>& task) {
    std::atomic<bool> completed{false};
    std::exception_ptr exception_holder = nullptr;
    
    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();
    
    // 创建完成回调
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true);
        } catch (...) {
            exception_holder = std::current_exception();
            completed.store(true);
        }
    }();
    
    // 启动任务
    manager.schedule_resume(completion_task.handle);
    
    // 等待完成并持续驱动协程管理器
    while (!completed.load()) {
        manager.drive();  // 驱动协程调度
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 最终清理
    manager.drive();
    
    // 重新抛出异常
    if (exception_holder) {
        std::rethrow_exception(exception_holder);
    }
}

// 显式实例化常用类型
template void run_until_complete<int>(Task<int>&);
template void run_until_complete<void>(Task<void>&);

} // namespace flowcoro
