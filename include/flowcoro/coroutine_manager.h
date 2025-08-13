#pragma once
#include <coroutine>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <atomic>
#include <array>
#include <vector>
#include <algorithm>
#include "load_balancer.h"
#include "performance_monitor.h"
#include "logger.h"

namespace flowcoro {

// 前向声明调度器API
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);
void drive_coroutine_pool();

// 协程管理器
class CoroutineManager {
private:
    // 智能负载均衡器实例
    SmartLoadBalancer load_balancer_;
    
public:
    CoroutineManager() = default;
    
    // 禁止拷贝和移动
    CoroutineManager(const CoroutineManager&) = delete;
    CoroutineManager& operator=(const CoroutineManager&) = delete;
    CoroutineManager(CoroutineManager&&) = delete;
    CoroutineManager& operator=(CoroutineManager&&) = delete;

    // 驱动协程调度
    void drive() {
        // 驱动新的协程池系统
        drive_coroutine_pool();

        // 保留原有的定时器和任务处理
        process_timer_queue();
        process_ready_queue();
        process_pending_tasks();
    }

    // 添加定时器
    void add_timer(std::chrono::steady_clock::time_point when, std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.emplace(when, handle);
    }

    // 调度协程恢复 - 集成协程池和智能负载均衡
    void schedule_resume(std::coroutine_handle<> handle) {
        if (!handle) {
            LOG_ERROR("Null handle in schedule_resume");
            return;
        }

        // 检查句柄地址有效性
        void* addr = handle.address();
        if (!addr) {
            LOG_ERROR("Invalid handle address in schedule_resume");
            return;
        }

        // 检查协程是否已完成
        if (handle.done()) {
            LOG_DEBUG("Handle already done in schedule_resume");
            return;
        }

        // 记录调度器调用
        PerformanceMonitor::get_instance().on_scheduler_invocation();

        // 使用增强的协程池进行调度
        schedule_coroutine_enhanced(handle);
    }

    // 获取负载均衡器引用
    SmartLoadBalancer& get_load_balancer() noexcept {
        return load_balancer_;
    }

    // 调度协程销毁（延迟销毁）
    void schedule_destroy(std::coroutine_handle<> handle) {
        if (!handle) return;

        std::lock_guard<std::mutex> lock(destroy_mutex_);
        destroy_queue_.push(handle);
    }

    // 获取全局管理器实例
    static CoroutineManager& get_instance() {
        static CoroutineManager instance;
        return instance;
    }

    ~CoroutineManager()
    {
        stop_timer_thread();
    }
    // 启动专用定时器线程
    void start_timer_thread()
    {
        if (dedicated_timer_thread_)
        {
            return; // 已启动
        }
        timer_thread_stop_.store(false, std::memory_order_release);
        dedicated_timer_thread_ = std::make_unique<std::thread>([this]
                                                                { dedicated_timer_thread_func(); });
        LOG_INFO("Dedicated timer thread started");
    }
    // 停止专用定时器线程
    void stop_timer_thread()
    {
        if (!dedicated_timer_thread_)
        {
            return;
        }
        timer_thread_stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(timer_thread_mutex_);
            timer_thread_cv_.notify_all();
        }
        if (dedicated_timer_thread_->joinable())
        {
            dedicated_timer_thread_->join();
        }
        dedicated_timer_thread_.reset();
        LOG_INFO("Dedicated timer thread stopped");
    }
    // 增强版添加定时器 - 支持专用线程
    uint64_t add_timer_enhanced(std::chrono::steady_clock::time_point when,
                                std::coroutine_handle<> handle)
    {
        if (!handle)
        {
            LOG_ERROR("Invalid handle in add_timer_enhanced");
            return 0;
        }
        uint64_t timer_id = timer_id_generator_.fetch_add(1, std::memory_order_relaxed);
        
        // 记录定时器事件
        PerformanceMonitor::get_instance().on_timer_event();
        
        // 如果专用定时器线程启动了，使用它
        if (dedicated_timer_thread_)
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timer_queue_.emplace(when, handle);
            timer_thread_cv_.notify_one(); // 通知专用线程
            return timer_id;
        }
        // 否则使用原有方式
        add_timer(when, handle);
        return timer_id;
    }

private:
    // 专用定时器线程主函数
    void dedicated_timer_thread_func()
    {
        LOG_INFO("Dedicated timer thread started");
        while (!timer_thread_stop_.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> lock(timer_mutex_);
            if (timer_queue_.empty())
            {
                // 等待新的定时器或停止信号
                timer_thread_cv_.wait(lock, [this]
                                      { return timer_thread_stop_.load(std::memory_order_acquire) ||
                                               !timer_queue_.empty(); });
                continue;
            }
            auto now = std::chrono::steady_clock::now();
            const auto &[when, handle] = timer_queue_.top();
            if (when <= now)
            {
                // 批量处理到期的定时器
                std::vector<std::coroutine_handle<>> expired_handles;
                while (!timer_queue_.empty() && timer_queue_.top().first <= now)
                {
                    expired_handles.push_back(timer_queue_.top().second);
                    timer_queue_.pop();
                }
                lock.unlock(); // 释放锁，避免在恢复协程时阻塞
                // 批量恢复协程（通过线程池异步执行）
                for (auto h : expired_handles)
                {
                    if (h && !h.done())
                    {
                        // 使用现有的协程池恢复
                        schedule_coroutine_enhanced(h);
                    }
                }
            }
            else
            {
                // 精确等待到下一个定时器时间
                timer_thread_cv_.wait_until(lock, when, [this]
                                            { return timer_thread_stop_.load(std::memory_order_acquire); });
            }
        }
        LOG_INFO("Dedicated timer thread stopped");
    }

    void process_timer_queue() {
        static constexpr size_t BATCH_SIZE = 32; // 批处理大小
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto now = std::chrono::steady_clock::now();

        // 批量处理到期的定时器
        while (!timer_queue_.empty() && batch_count < BATCH_SIZE) {
            const auto& [when, handle] = timer_queue_.top();
            if (when > now) break;

            // 收集到批处理数组中
            if (handle && !handle.done()) {
                batch[batch_count++] = handle;
            }
            timer_queue_.pop();
        }
        
        // 批量移动到ready队列
        if (batch_count > 0) {
            std::lock_guard<std::mutex> ready_lock(ready_mutex_);
            for (size_t i = 0; i < batch_count; ++i) {
                ready_queue_.push(batch[i]);
            }
        }
    }

    void process_ready_queue() {
        static constexpr size_t BATCH_SIZE = 64; // 增大批处理大小
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        // 批量获取就绪的协程
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            while (!ready_queue_.empty() && batch_count < BATCH_SIZE) {
                batch[batch_count++] = ready_queue_.front();
                ready_queue_.pop();
            }
        }

        // 批量处理协程，无锁执行
        for (size_t i = 0; i < batch_count; ++i) {
            auto handle = batch[i];
            
            // 快速检查 - 最小化验证开销
            if (!handle) continue;
            if (handle.done()) continue;

            // 安全地恢复协程
            handle.resume();
        }
    }

    void process_pending_tasks() {
        static constexpr size_t BATCH_SIZE = 32; // 销毁操作批处理
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        // 批量获取待销毁的协程
        {
            std::lock_guard<std::mutex> lock(destroy_mutex_);
            while (!destroy_queue_.empty() && batch_count < BATCH_SIZE) {
                batch[batch_count++] = destroy_queue_.front();
                destroy_queue_.pop();
            }
        }

        // 批量销毁协程，无锁执行
        for (size_t i = 0; i < batch_count; ++i) {
            auto handle = batch[i];
            
            if (!handle) continue;
            
            // 快速销毁，不检查地址有效性（减少开销）
            handle.destroy();
        }
    }

    // 定时器队列（最小堆）
    std::priority_queue<
        std::pair<std::chrono::steady_clock::time_point, std::coroutine_handle<>>,
        std::vector<std::pair<std::chrono::steady_clock::time_point, std::coroutine_handle<>>>,
        std::greater<>
    > timer_queue_;
    std::mutex timer_mutex_;

    // 就绪队列
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex ready_mutex_;

    // 延迟销毁队列
    std::queue<std::coroutine_handle<>> destroy_queue_;
    std::mutex destroy_mutex_;

    // 专用定时器线程
    std::unique_ptr<std::thread> dedicated_timer_thread_;
    std::atomic<bool> timer_thread_stop_{false};        
    // 定时器线程专用的条件变量（独立于现有的timer_mutex_）
    std::condition_variable timer_thread_cv_;    
    mutable std::mutex timer_thread_mutex_;        
    // 定时器ID生成器
    std::atomic<uint64_t> timer_id_generator_{1};
};

} // namespace flowcoro
