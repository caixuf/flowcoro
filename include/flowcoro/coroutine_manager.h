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

// API
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);
void drive_coroutine_pool();

// 
class CoroutineManager {
private:
    // 
    SmartLoadBalancer load_balancer_;
    
public:
    CoroutineManager() = default;
    
    // 
    CoroutineManager(const CoroutineManager&) = delete;
    CoroutineManager& operator=(const CoroutineManager&) = delete;
    CoroutineManager(CoroutineManager&&) = delete;
    CoroutineManager& operator=(CoroutineManager&&) = delete;

    // 
    void drive() {
        // 
        drive_coroutine_pool();

        // 
        process_timer_queue();
        process_ready_queue();
        process_pending_tasks();
    }

    // 
    void add_timer(std::chrono::steady_clock::time_point when, std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.emplace(when, handle);
    }

    //  - 
    void schedule_resume(std::coroutine_handle<> handle) {
        if (!handle) {
            LOG_ERROR("Null handle in schedule_resume");
            return;
        }

        // 
        void* addr = handle.address();
        if (!addr) {
            LOG_ERROR("Invalid handle address in schedule_resume");
            return;
        }

        // 
        if (handle.done()) {
            LOG_DEBUG("Handle already done in schedule_resume");
            return;
        }

        // 
        PerformanceMonitor::get_instance().on_scheduler_invocation();

        // 
        schedule_coroutine_enhanced(handle);
    }

    // 
    SmartLoadBalancer& get_load_balancer() noexcept {
        return load_balancer_;
    }

    // 
    void schedule_destroy(std::coroutine_handle<> handle) {
        if (!handle) return;

        std::lock_guard<std::mutex> lock(destroy_mutex_);
        destroy_queue_.push(handle);
    }

    // 
    static CoroutineManager& get_instance() {
        static CoroutineManager instance;
        return instance;
    }

    ~CoroutineManager()
    {
        stop_timer_thread();
    }
    // 
    void start_timer_thread()
    {
        if (dedicated_timer_thread_)
        {
            return; // 
        }
        timer_thread_stop_.store(false, std::memory_order_release);
        dedicated_timer_thread_ = std::make_unique<std::thread>([this]
                                                                { dedicated_timer_thread_func(); });
        LOG_INFO("Dedicated timer thread started");
    }
    // 
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
    //  - 
    uint64_t add_timer_enhanced(std::chrono::steady_clock::time_point when,
                                std::coroutine_handle<> handle)
    {
        if (!handle)
        {
            LOG_ERROR("Invalid handle in add_timer_enhanced");
            return 0;
        }
        uint64_t timer_id = timer_id_generator_.fetch_add(1, std::memory_order_relaxed);
        
        // 
        PerformanceMonitor::get_instance().on_timer_event();
        
        // 
        if (dedicated_timer_thread_)
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timer_queue_.emplace(when, handle);
            timer_thread_cv_.notify_one(); // 
            return timer_id;
        }
        // 
        add_timer(when, handle);
        return timer_id;
    }

    //  - 
    void process_timer_queue() {
        static constexpr size_t BATCH_SIZE = 32; // 
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto now = std::chrono::steady_clock::now();

        // 
        while (!timer_queue_.empty() && batch_count < BATCH_SIZE) {
            const auto& [when, handle] = timer_queue_.top();
            if (when > now) break;

            // 
            if (handle && !handle.done()) {
                batch[batch_count++] = handle;
            }
            timer_queue_.pop();
        }
        
        // ready
        if (batch_count > 0) {
            std::lock_guard<std::mutex> ready_lock(ready_mutex_);
            for (size_t i = 0; i < batch_count; ++i) {
                ready_queue_.push(batch[i]);
            }
        }
    }

    void process_ready_queue() {
        static constexpr size_t BATCH_SIZE = 64; // 
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        // 
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            while (!ready_queue_.empty() && batch_count < BATCH_SIZE) {
                batch[batch_count++] = ready_queue_.front();
                ready_queue_.pop();
            }
        }

        // 
        for (size_t i = 0; i < batch_count; ++i) {
            auto handle = batch[i];
            
            //  - 
            if (!handle) continue;
            if (handle.done()) continue;

            // 
            handle.resume();
        }
    }

    void process_pending_tasks() {
        static constexpr size_t BATCH_SIZE = 64; // 
        std::array<std::coroutine_handle<>, BATCH_SIZE> batch;
        size_t batch_count = 0;
        
        // 
        {
            std::lock_guard<std::mutex> lock(destroy_mutex_);
            while (!destroy_queue_.empty() && batch_count < BATCH_SIZE) {
                batch[batch_count++] = destroy_queue_.front();
                destroy_queue_.pop();
            }
        }

        // 
        for (size_t i = 0; i < batch_count; ++i) {
            auto handle = batch[i];
            
            if (__builtin_expect(!handle || !handle.address(), false)) [[unlikely]] continue;
            
            try {
                // 
                handle.destroy();
            } catch (...) {
                // faster loggingrelease
                #ifndef NDEBUG
                LOG_ERROR("Exception during coroutine destruction");
                #endif
            }
        }
    }

private:
    // 
    void dedicated_timer_thread_func()
    {
        LOG_INFO("Dedicated timer thread started");
        while (!timer_thread_stop_.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> lock(timer_mutex_);
            if (timer_queue_.empty())
            {
                // 
                timer_thread_cv_.wait(lock, [this]
                                      { return timer_thread_stop_.load(std::memory_order_acquire) ||
                                               !timer_queue_.empty(); });
                continue;
            }
            auto now = std::chrono::steady_clock::now();
            const auto &[when, handle] = timer_queue_.top();
            if (when <= now)
            {
                // 
                std::vector<std::coroutine_handle<>> expired_handles;
                while (!timer_queue_.empty() && timer_queue_.top().first <= now)
                {
                    expired_handles.push_back(timer_queue_.top().second);
                    timer_queue_.pop();
                }
                lock.unlock(); // 
                // 
                for (auto h : expired_handles)
                {
                    if (h && !h.done())
                    {
                        // 
                        schedule_coroutine_enhanced(h);
                    }
                }
            }
            else
            {
                // 
                timer_thread_cv_.wait_until(lock, when, [this]
                                            { return timer_thread_stop_.load(std::memory_order_acquire); });
            }
        }
        LOG_INFO("Dedicated timer thread stopped");
    }

    // 
    std::priority_queue<
        std::pair<std::chrono::steady_clock::time_point, std::coroutine_handle<>>,
        std::vector<std::pair<std::chrono::steady_clock::time_point, std::coroutine_handle<>>>,
        std::greater<>
    > timer_queue_;
    std::mutex timer_mutex_;

    // 
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex ready_mutex_;

    // 
    std::queue<std::coroutine_handle<>> destroy_queue_;
    std::mutex destroy_mutex_;

    // 
    std::unique_ptr<std::thread> dedicated_timer_thread_;
    std::atomic<bool> timer_thread_stop_{false};        
    // timer_mutex_
    std::condition_variable timer_thread_cv_;    
    mutable std::mutex timer_thread_mutex_;        
    // ID
    std::atomic<uint64_t> timer_id_generator_{1};
};

} // namespace flowcoro
