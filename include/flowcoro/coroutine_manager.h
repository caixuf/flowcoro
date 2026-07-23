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
    //
    // 入队的都是「已被宿主放弃」的帧（safe_destroy 来自 ~Task / operator=）。
    // process_pending_tasks 无条件销毁入队帧——这会运行帧上局部对象的析构，
    // 由各 awaitable 的析构函数负责摘除外部恢复源（bus/timer/IO），
    // 这是 Task 的 RAII 契约（test_bus_awaitable 析构测试即验证之）。
    //
    // 注：曾尝试「只销毁 settled 的、未 settled 的 requeue」（FC-1 原方案），
    // 但会破坏上述 RAII 契约并导致泄漏（详见 process_pending_tasks 注释）。
    // settled 标志仍保留在 promise 中，用作线程安全的完成指示（替代非线程
    // 安全的 done()），但不用于 gate 销毁。
    void schedule_destroy(std::coroutine_handle<> handle) {
        if (!handle) return;

        std::lock_guard<std::mutex> lock(destroy_mutex_);
        destroy_queue_.push(handle);
        // FC-2: lazy 启动后台回收线程。
        // 用 call_once 保证只启动一次，避免静态初始化顺序问题。
        ensure_reaper_started();
    }

    // 获取全局管理器实例
    static CoroutineManager& get_instance() {
        static CoroutineManager instance;
        return instance;
    }

    ~CoroutineManager()
    {
        // FC-2: 停止后台回收线程
        {
            std::lock_guard<std::mutex> lock(reaper_mutex_);
            reaper_stop_.store(true, std::memory_order_release);
        }
        reaper_cv_.notify_one();
        if (reaper_thread_.joinable()) {
            reaper_thread_.join();
        }
        // 最后 drain 一次（reaper 已停，单线程安全）
        process_pending_tasks();
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
        static constexpr size_t BATCH_SIZE = 64;
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;

        // 批量获取待销毁的协程
        {
            std::lock_guard<std::mutex> lock(destroy_mutex_);
            while (!destroy_queue_.empty() && batch_count < BATCH_SIZE) {
                batch[batch_count++] = std::move(destroy_queue_.front());
                destroy_queue_.pop();
            }
        }

        // 销毁队列中的帧。
        //
        // 设计说明（FC-1 复盘）：
        // 最初按 FC-1 设计为「只销毁 settled 的、未 settled 的 requeue」，
        // 但经验证该策略会破坏 Task 的 RAII 契约：
        //   - schedule_destroy 的唯一调用源是 safe_destroy（即 ~Task / operator=），
        //     入队的都是「已被宿主放弃」的帧。
        //   - 一个挂在 await 点的帧若被 requeue，它永远不会自己 settled
        //     （没人再 resume 它）→ 永不销毁 → 内存泄漏。
        //   - 更严重的是：~Awaitable 析构（如 BusAwaitable::unsubscribe）永远不会
        //     被调用 → 外部恢复源（bus/timer/IO）仍持有该 handle → 后续 resume
        //     命中已释放帧 = 真正的 UAF。test_bus_awaitable 的析构测试即验证此契约。
        //
        // 因此正确做法是：无条件销毁入队帧。这会运行帧上局部对象的析构，
        // 由各 awaitable 的析构函数负责「摘除外部恢复源」（RAII 契约）。
        // settled 标志仍保留在 promise 中，用作线程安全的完成指示（替代非线程
        // 安全的 done()），但不再用于 gate 销毁。
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
    // FC-2: 后台回收线程——周期 drain destroy_queue_
    void ensure_reaper_started() {
        std::call_once(reaper_once_, [this] {
            reaper_thread_ = std::thread([this] { reaper_loop(); });
        });
    }

    void reaper_loop() {
        using namespace std::chrono_literals;
        while (!reaper_stop_.load(std::memory_order_acquire)) {
            process_pending_tasks();
            std::unique_lock<std::mutex> lock(reaper_mutex_);
            reaper_cv_.wait_for(lock, 100ms, [this] {
                return reaper_stop_.load(std::memory_order_acquire);
            });
        }
        // 退出前最后 drain 一次
        process_pending_tasks();
    }

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

    // FC-2: 后台回收线程
    std::thread reaper_thread_;
    std::atomic<bool> reaper_stop_{false};
    std::condition_variable reaper_cv_;
    std::mutex reaper_mutex_;
    std::once_flag reaper_once_;

    // 专用定时器线程
    std::unique_ptr<std::thread> dedicated_timer_thread_;
    std::atomic<bool> timer_thread_stop_{false};        
    // 专用定时器线程的条件变量（与 timer_mutex_ 配合使用）
    std::condition_variable timer_thread_cv_;    
    // 定时器ID生成器
    std::atomic<uint64_t> timer_id_generator_{1};
};

} // namespace flowcoro
