/**
 * @file advanced_lifecycle.h
 * @brief 基于现有代码模式改进的高级协程生命周期管理
 * @author FlowCoro Team
 * @date 2025-07-21
 * 
 * 从现有优秀模式中汲取灵感：
 * - Transaction类的RAII模式
 * - AsyncPromise的状态同步模式  
 * - Task析构函数的安全检查模式
 */

#pragma once

#include "core.h"
#include "cancellation.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sstream>

namespace flowcoro::lifecycle {

/**
 * @brief 增强的协程句柄包装器 (受AsyncPromise状态管理启发)
 * 
 * 从AsyncPromise学习了状态同步和线程安全的技巧
 */
class advanced_coroutine_handle {
private:
    struct HandleState {
        std::atomic<bool> destroyed_{false};
        std::atomic<bool> ready_{false};
        std::coroutine_handle<> handle_{};
        mutable std::mutex mutex_;
        std::atomic<int> ref_count_{1};  // 引用计数管理
        
        HandleState() = default;
        explicit HandleState(std::coroutine_handle<> h) : handle_(h) {}
    };
    
    std::shared_ptr<HandleState> state_;
    
public:
    advanced_coroutine_handle() = default;
    
    explicit advanced_coroutine_handle(std::coroutine_handle<> handle) 
        : state_(std::make_shared<HandleState>(handle)) {}
    
    // 拷贝构造 - 增加引用计数
    advanced_coroutine_handle(const advanced_coroutine_handle& other) noexcept
        : state_(other.state_) {
        if (state_) {
            state_->ref_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    advanced_coroutine_handle& operator=(const advanced_coroutine_handle& other) noexcept {
        if (this != &other) {
            reset();
            state_ = other.state_;
            if (state_) {
                state_->ref_count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }
    
    // 移动构造
    advanced_coroutine_handle(advanced_coroutine_handle&& other) noexcept
        : state_(std::move(other.state_)) {}
    
    advanced_coroutine_handle& operator=(advanced_coroutine_handle&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    
    ~advanced_coroutine_handle() {
        reset();
    }
    
    // 安全重置 (学习Transaction的析构模式)
    void reset() noexcept {
        if (state_) {
            int remaining_refs = state_->ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            if (remaining_refs == 1) {
                // 最后一个引用，执行清理
                safe_destroy();
            }
            state_.reset();
        }
    }
    
    // 学习现有Task析构函数的安全检查模式
    void safe_destroy() noexcept {
        if (!state_) return;
        
        bool expected = false;
        if (state_->destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            
            if (state_->handle_) {
                try {
                    // 学习现有代码的地址检查模式
                    if (state_->handle_.address() != nullptr && !state_->handle_.done()) {
                        state_->handle_.destroy();
                    }
                } catch (...) {
                    // 学习现有代码：忽略析构时的异常
                    LOG_ERROR("Exception during advanced coroutine handle destruction");
                }
                state_->handle_ = {};
            }
        }
    }
    
    // 状态查询方法 (学习AsyncPromise的ready模式)
    bool valid() const noexcept {
        return state_ && 
               state_->handle_ && 
               !state_->destroyed_.load(std::memory_order_acquire);
    }
    
    bool ready() const noexcept {
        return state_ && state_->ready_.load(std::memory_order_acquire);
    }
    
    bool done() const noexcept {
        if (!valid()) return true;
        
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->handle_.done();
    }
    
    // 安全恢复 (学习AsyncPromise的调度模式)
    void resume() {
        if (!valid()) return;
        
        std::coroutine_handle<> handle_to_resume{};
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (!state_->handle_.done()) {
                handle_to_resume = state_->handle_;
            }
        }
        
        if (handle_to_resume) {
            // 学习AsyncPromise：在锁外调度以避免死锁
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    try {
                        handle_to_resume.resume();
                    } catch (...) {
                        LOG_ERROR("Exception during coroutine resume");
                    }
                }
            });
        }
    }
    
    // 获取地址用于调试
    void* address() const noexcept {
        if (!state_) return nullptr;
        
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->handle_ ? state_->handle_.address() : nullptr;
    }
    
    // 引用计数信息
    int ref_count() const noexcept {
        return state_ ? state_->ref_count_.load(std::memory_order_acquire) : 0;
    }
};

/**
 * @brief 协程生命周期守护者 (学习Transaction的RAII模式)
 * 
 * 自动管理协程的整个生命周期，包括统计和清理
 */
class coroutine_lifecycle_guard {
private:
    std::shared_ptr<struct LifecycleData> data_;
    
    struct LifecycleData {
        advanced_coroutine_handle handle;
        coroutine_state_manager state_manager;
        cancellation_token cancel_token;
        std::chrono::steady_clock::time_point creation_time;
        std::atomic<bool> completed{false};
        std::string debug_name;
        std::function<void()> cleanup_callback; // 池化清理回调
        
        LifecycleData(advanced_coroutine_handle h, const std::string& name = "")
            : handle(std::move(h))
            , creation_time(std::chrono::steady_clock::now())
            , debug_name(name) {
            
            // 注册到全局管理器
            coroutine_lifecycle_manager::get().on_coroutine_created();
        }
        
        ~LifecycleData() {
            mark_completed();
            
            // 调用池化清理回调
            if (cleanup_callback) {
                try {
                    cleanup_callback();
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in pooled coroutine cleanup: %s", e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception in pooled coroutine cleanup");
                }
            }
        }
        
        void mark_completed() noexcept {
            bool expected = false;
            if (completed.compare_exchange_strong(expected, true)) {
                auto final_state = state_manager.get_state();
                
                switch (final_state) {
                    case coroutine_state::completed:
                        coroutine_lifecycle_manager::get().on_coroutine_completed();
                        break;
                    case coroutine_state::cancelled:
                        coroutine_lifecycle_manager::get().on_coroutine_cancelled();
                        break;
                    case coroutine_state::destroyed:
                        coroutine_lifecycle_manager::get().on_coroutine_failed();
                        break;
                    default:
                        coroutine_lifecycle_manager::get().on_coroutine_completed();
                        break;
                }
                
                LOG_TRACE("Coroutine lifecycle completed: %s, duration: %lld ms, state: %s",
                         debug_name.empty() ? "unnamed" : debug_name.c_str(),
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - creation_time
                         ).count(),
                         state_name(final_state));
            }
        }
    };
    
public:
    explicit coroutine_lifecycle_guard(advanced_coroutine_handle handle, const std::string& debug_name = "")
        : data_(std::make_shared<LifecycleData>(std::move(handle), debug_name)) {}
    
    // 拷贝构造 - 共享生命周期数据
    coroutine_lifecycle_guard(const coroutine_lifecycle_guard&) = default;
    coroutine_lifecycle_guard& operator=(const coroutine_lifecycle_guard&) = default;
    
    // 移动构造
    coroutine_lifecycle_guard(coroutine_lifecycle_guard&&) = default;
    coroutine_lifecycle_guard& operator=(coroutine_lifecycle_guard&&) = default;
    
    ~coroutine_lifecycle_guard() = default;
    
    // 池化支持：创建带有清理回调的守护
    static coroutine_lifecycle_guard create_pooled(
        advanced_coroutine_handle handle,
        const std::string& debug_name,
        std::function<void()> cleanup_callback) {
        
        coroutine_lifecycle_guard guard(std::move(handle), debug_name);
        guard.data_->cleanup_callback = std::move(cleanup_callback);
        return guard;
    }
    
    // 状态访问方法
    coroutine_state get_state() const {
        return data_ ? data_->state_manager.get_state() : coroutine_state::destroyed;
    }
    
    std::chrono::milliseconds get_lifetime() const {
        return data_ ? data_->state_manager.get_lifetime() : std::chrono::milliseconds{0};
    }
    
    bool is_active() const {
        return data_ && data_->state_manager.is_active() && data_->handle.valid();
    }
    
    // 控制方法
    void set_cancellation_token(cancellation_token token) {
        if (data_) {
            data_->cancel_token = std::move(token);
        }
    }
    
    void cancel() {
        if (data_) {
            data_->state_manager.force_transition(coroutine_state::cancelled);
            data_->mark_completed();
        }
    }
    
    // 句柄访问
    advanced_coroutine_handle& handle() {
        static advanced_coroutine_handle invalid_handle;
        return data_ ? data_->handle : invalid_handle;
    }
    
    const advanced_coroutine_handle& handle() const {
        static advanced_coroutine_handle invalid_handle;
        return data_ ? data_->handle : invalid_handle;
    }
    
    // 调试信息
    std::string get_debug_info() const {
        if (!data_) return "Invalid guard";
        
        // 简化格式化字符串，避免依赖fmt
        std::ostringstream oss;
        oss << "Coroutine '" << (data_->debug_name.empty() ? "unnamed" : data_->debug_name)
            << "': state=" << state_name(data_->state_manager.get_state())
            << ", lifetime=" << data_->state_manager.get_lifetime().count() << "ms"
            << ", refs=" << data_->handle.ref_count()
            << ", valid=" << (data_->handle.valid() ? "true" : "false");
        return oss.str();
    }
};

/**
 * @brief 协程超时管理器 (学习SleepManager的模式)
 * 
 * 提供更精确的超时控制和取消机制
 */
class advanced_timeout_manager {
private:
    struct TimeoutRequest {
        coroutine_lifecycle_guard guard;
        std::chrono::steady_clock::time_point deadline;
        cancellation_source timeout_source;
        std::atomic<bool> completed{false};
        std::string operation_name;
        
        TimeoutRequest(coroutine_lifecycle_guard g, 
                      std::chrono::milliseconds timeout,
                      std::string name = "")
            : guard(std::move(g))
            , deadline(std::chrono::steady_clock::now() + timeout)
            , operation_name(std::move(name)) {
            
            // 设置超时取消令牌
            guard.set_cancellation_token(timeout_source.get_token());
        }
    };
    
    mutable std::mutex requests_mutex_;
    std::vector<std::shared_ptr<TimeoutRequest>> active_requests_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
    std::thread worker_thread_;
    
    advanced_timeout_manager() {
        worker_thread_ = std::thread([this]() { worker_loop(); });
    }
    
    void worker_loop() {
        std::unique_lock<std::mutex> lock(requests_mutex_);
        
        while (!shutdown_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            
            // 处理超时请求
            auto it = active_requests_.begin();
            while (it != active_requests_.end()) {
                auto& request = *it;
                
                if (request->completed.load(std::memory_order_acquire)) {
                    // 请求已完成，移除
                    it = active_requests_.erase(it);
                } else if (request->deadline <= now) {
                    // 超时了，触发取消
                    LOG_WARN("Coroutine operation '%s' timed out after %lld ms",
                            request->operation_name.c_str(),
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - (request->deadline - std::chrono::milliseconds(1000))
                            ).count());
                    
                    request->timeout_source.cancel();
                    request->guard.cancel();
                    request->completed.store(true);
                    
                    it = active_requests_.erase(it);
                } else {
                    ++it;
                }
            }
            
            // 等待下一次检查或新请求
            if (!active_requests_.empty()) {
                auto next_deadline = std::min_element(
                    active_requests_.begin(), active_requests_.end(),
                    [](const auto& a, const auto& b) {
                        return a->deadline < b->deadline;
                    }
                )->get()->deadline;
                
                cv_.wait_until(lock, next_deadline);
            } else {
                cv_.wait(lock);
            }
        }
    }
    
public:
    static advanced_timeout_manager& get() {
        static advanced_timeout_manager instance;
        return instance;
    }
    
    ~advanced_timeout_manager() {
        shutdown_.store(true, std::memory_order_release);
        cv_.notify_all();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    // 注册超时请求
    std::shared_ptr<TimeoutRequest> register_timeout(
        coroutine_lifecycle_guard guard,
        std::chrono::milliseconds timeout,
        const std::string& operation_name = "") {
        
        auto request = std::make_shared<TimeoutRequest>(
            std::move(guard), timeout, operation_name
        );
        
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            active_requests_.push_back(request);
        }
        
        cv_.notify_one();
        return request;
    }
    
    // 获取统计信息
    struct stats {
        size_t active_timeouts;
        size_t total_registered;
        size_t timed_out_count;
    };
    
    stats get_stats() const {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        
        // 这里简化实现，实际应该维护更完整的统计
        return stats{
            .active_timeouts = active_requests_.size(),
            .total_registered = 0,  // 需要持久化计数器
            .timed_out_count = 0    // 需要持久化计数器
        };
    }
};

// 便利函数
inline coroutine_lifecycle_guard make_guarded_coroutine(
    std::coroutine_handle<> handle, 
    const std::string& debug_name = "") {
    return coroutine_lifecycle_guard{
        advanced_coroutine_handle{handle},
        debug_name
    };
}

template<typename Duration>
auto with_advanced_timeout(
    coroutine_lifecycle_guard guard,
    Duration timeout,
    const std::string& operation_name = "") {
    
    auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
    return advanced_timeout_manager::get().register_timeout(
        std::move(guard), timeout_ms, operation_name
    );
}

} // namespace flowcoro::lifecycle
