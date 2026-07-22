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
#include <unordered_set>
#include "load_balancer.h"
#include "performance_monitor.h"
#include "logger.h"

namespace flowcoro {

// 前向声明调度器API
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);
void drive_coroutine_pool();

// 定时器条目：携带 timer_id 以支持取消
struct TimerEntry {
    std::chrono::steady_clock::time_point when;
    uint64_t timer_id;
    std::coroutine_handle<> handle;

    // 最小堆：按 when 升序
    bool operator>(const TimerEntry& other) const noexcept {
        return when > other.when;
    }
};

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
    // 返回生成的 timer_id，供调用方在需要时取消该定时器
    uint64_t add_timer(std::chrono::steady_clock::time_point when, std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        uint64_t id = timer_id_generator_.fetch_add(1, std::memory_order_relaxed);
        timer_queue_.emplace(when, id, handle);
        return id;
    }

    // 取消定时器
    // 标记 timer_id 为已取消；下次扫描队列时跳过。
    // 线程安全：持有 timer_mutex_。即使定时器线程正在处理也无害。
    void cancel_timer(uint64_t timer_id) {
        if (timer_id == 0) return; // 0 表示无定时器
        std::lock_guard<std::mutex> lock(timer_mutex_);
        cancelled_timers_.insert(timer_id);
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
        {
            // 必须使用与定时器线程 wait 调用相同的互斥量 (timer_mutex_)，
            // 才能保证 notify_all 不会在设置 stop 标志与线程进入等待之间丢失。
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timer_thread_stop_.store(true, std::memory_order_release);
        }
        timer_thread_cv_.notify_all();
        if (dedicated_timer_thread_->joinable())
        {
            dedicated_timer_thread_->join();
        }
        dedicated_timer_thread_.reset();
        LOG_INFO("Dedicated timer thread stopped");
    }
    // 增强版添加定时器 - 支持专用线程
    // 返回的 timer_id 与队列中条目的 id 一致，可用于 cancel_timer。
    uint64_t add_timer_enhanced(std::chrono::steady_clock::time_point when,
                                std::coroutine_handle<> handle)
    {
        if (!handle)
        {
            LOG_ERROR("Invalid handle in add_timer_enhanced");
            return 0;
        }

        // 记录定时器事件
        PerformanceMonitor::get_instance().on_timer_event();

        // 如果专用定时器线程启动了，使用它
        if (dedicated_timer_thread_)
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            uint64_t timer_id = timer_id_generator_.fetch_add(1, std::memory_order_relaxed);
            timer_queue_.emplace(when, timer_id, handle);
            timer_thread_cv_.notify_one(); // 通知专用线程
            return timer_id;
        }
        // 否则使用原有方式 — add_timer 生成并返回真正入队的 timer_id
        return add_timer(when, handle);
    }

    // 公开的队列处理方法 - 用于外部驱动
    void process_timer_queue() {
        static constexpr size_t BATCH_SIZE = 32; // 批处理大小
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto now = std::chrono::steady_clock::now();

        // 批量处理到期的定时器
        while (!timer_queue_.empty() && batch_count < BATCH_SIZE) {
            const auto& entry = timer_queue_.top();
            if (entry.when > now) break;

            // 跳过已取消的定时器，并从取消集合中移除（条目已出队）
            if (cancelled_timers_.erase(entry.timer_id) == 0) {
                // 未被取消 — 正常调度
                if (entry.handle && !entry.handle.done()) {
                    batch[batch_count++] = entry.handle;
                }
            }
            // 已取消的条目：静默丢弃
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
        static constexpr size_t BATCH_SIZE = 64; // 增加批处理大小，减少锁竞争
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
        // 注意：不调用 handle.done() 检查——它在协程池模式下跨线程不安全
        // （GCC 的 done() 读取协程帧内部 __resume_index）。
        // 入队的协程都应已完成（挂起在 final_suspend），destroy 是安全的。
        for (size_t i = 0; i < batch_count; ++i) {
            auto handle = batch[i];

            if (__builtin_expect(!handle || !handle.address(), false)) [[unlikely]] continue;

            try {
                handle.destroy();
            } catch (...) {
                #ifndef NDEBUG
                LOG_ERROR("Exception during coroutine destruction");
                #endif
            }
        }
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
            const auto& top = timer_queue_.top();
            if (top.when <= now)
            {
                // 批量处理到期的定时器
                std::vector<TimerEntry> expired;
                while (!timer_queue_.empty() && timer_queue_.top().when <= now)
                {
                    expired.push_back(std::move(const_cast<TimerEntry&>(timer_queue_.top())));
                    timer_queue_.pop();
                }
                lock.unlock(); // 释放锁，避免在恢复协程时阻塞

                // 过滤已取消的定时器
                std::vector<std::coroutine_handle<>> to_resume;
                {
                    std::lock_guard<std::mutex> lk2(timer_mutex_);
                    for (auto& e : expired) {
                        if (cancelled_timers_.erase(e.timer_id) > 0) {
                            continue; // 已取消，跳过
                        }
                        to_resume.push_back(e.handle);
                    }
                }
                // 批量恢复协程（通过线程池异步执行）
                for (auto h : to_resume)
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
                timer_thread_cv_.wait_until(lock, top.when, [this]
                                            { return timer_thread_stop_.load(std::memory_order_acquire); });
            }
        }
        LOG_INFO("Dedicated timer thread stopped");
    }

    // 定时器队列（最小堆）— 条目携带 timer_id 以支持取消
    std::priority_queue<TimerEntry,
        std::vector<TimerEntry>,
        std::greater<TimerEntry>> timer_queue_;
    std::mutex timer_mutex_;

    // 已取消的定时器 ID 集合 — 由 timer_mutex_ 保护
    std::unordered_set<uint64_t> cancelled_timers_;

    // 就绪队列
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex ready_mutex_;

    // 延迟销毁队列
    std::queue<std::coroutine_handle<>> destroy_queue_;
    std::mutex destroy_mutex_;

    // 专用定时器线程
    std::unique_ptr<std::thread> dedicated_timer_thread_;
    std::atomic<bool> timer_thread_stop_{false};        
    // 专用定时器线程的条件变量（与 timer_mutex_ 配合使用）
    std::condition_variable timer_thread_cv_;    
    // 定时器ID生成器
    std::atomic<uint64_t> timer_id_generator_{1};
};

} // namespace flowcoro
