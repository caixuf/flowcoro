#pragma once

/**
 * @file lifecycle.h
 * @brief FlowCoro协程生命周期管理增强组件
 * @author FlowCoro Team
 * @version 2.1.0
 * @date 2025-07-21
 * 
 * 提供增强的协程生命周期管理功能：
 * - 安全的协程句柄管理
 * - 完整的取消机制支持
 * - 协程状态追踪
 * - 超时处理
 * - 向后兼容的API
 */

// 核心生命周期组件
#include "lifecycle/core.h"
#include "lifecycle/cancellation.h"
#include "lifecycle/enhanced_task.h"
#include "core.h"  // 为了使用GlobalThreadPool等核心组件

namespace flowcoro::lifecycle {

/**
 * @brief 协程生命周期管理器
 * 
 * 提供全局的协程生命周期管理功能，包括：
 * - 协程统计信息
 * - 全局取消机制
 * - 资源清理
 */
class coroutine_lifecycle_manager {
private:
    std::atomic<size_t> active_coroutines_{0};
    std::atomic<size_t> total_created_{0};
    std::atomic<size_t> completed_coroutines_{0};
    std::atomic<size_t> cancelled_coroutines_{0};
    std::atomic<size_t> failed_coroutines_{0};
    
    std::mutex global_cancellation_mutex_;
    std::vector<std::weak_ptr<cancellation_state>> global_cancellations_;
    
    // 单例模式
    static std::unique_ptr<coroutine_lifecycle_manager> instance_;
    static std::once_flag init_flag_;
    
    coroutine_lifecycle_manager() = default;
    
public:
    // 获取全局实例
    static coroutine_lifecycle_manager& get() {
        std::call_once(init_flag_, []() {
            instance_ = std::unique_ptr<coroutine_lifecycle_manager>(
                new coroutine_lifecycle_manager()
            );
        });
        return *instance_;
    }
    
    // 协程生命周期事件回调
    void on_coroutine_created() noexcept {
        active_coroutines_.fetch_add(1, std::memory_order_relaxed);
        total_created_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void on_coroutine_completed() noexcept {
        active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void on_coroutine_cancelled() noexcept {
        active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
        cancelled_coroutines_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void on_coroutine_failed() noexcept {
        active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
        failed_coroutines_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // 统计信息
    struct stats {
        size_t active_coroutines;
        size_t total_created;
        size_t completed_coroutines;
        size_t cancelled_coroutines;
        size_t failed_coroutines;
        double completion_rate;
        double failure_rate;
        double cancellation_rate;
    };
    
    stats get_stats() const noexcept {
        size_t active = active_coroutines_.load();
        size_t total = total_created_.load();
        size_t completed = completed_coroutines_.load();
        size_t cancelled = cancelled_coroutines_.load();
        size_t failed = failed_coroutines_.load();
        
        double completion_rate = total > 0 ? double(completed) / total : 0.0;
        double failure_rate = total > 0 ? double(failed) / total : 0.0;
        double cancellation_rate = total > 0 ? double(cancelled) / total : 0.0;
        
        return stats{
            .active_coroutines = active,
            .total_created = total,
            .completed_coroutines = completed,
            .cancelled_coroutines = cancelled,
            .failed_coroutines = failed,
            .completion_rate = completion_rate,
            .failure_rate = failure_rate,
            .cancellation_rate = cancellation_rate
        };
    }
    
    // 全局取消机制
    void register_for_global_cancellation(std::weak_ptr<cancellation_state> state) {
        std::lock_guard<std::mutex> lock(global_cancellation_mutex_);
        global_cancellations_.push_back(std::move(state));
        
        // 清理已失效的弱引用
        global_cancellations_.erase(
            std::remove_if(global_cancellations_.begin(), global_cancellations_.end(),
                          [](const std::weak_ptr<cancellation_state>& weak) {
                              return weak.expired();
                          }),
            global_cancellations_.end()
        );
    }
    
    void cancel_all() noexcept {
        std::lock_guard<std::mutex> lock(global_cancellation_mutex_);
        for (auto& weak_state : global_cancellations_) {
            if (auto state = weak_state.lock()) {
                state->request_cancellation();
            }
        }
        global_cancellations_.clear();
    }
    
    size_t get_registered_count() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(global_cancellation_mutex_));
        return std::count_if(global_cancellations_.begin(), global_cancellations_.end(),
                            [](const std::weak_ptr<cancellation_state>& weak) {
                                return !weak.expired();
                            });
    }
};

// 静态成员定义
std::unique_ptr<coroutine_lifecycle_manager> coroutine_lifecycle_manager::instance_;
std::once_flag coroutine_lifecycle_manager::init_flag_;

/**
 * @brief 协程超时处理器
 * 
 * 为协程提供超时功能，包装任何协程任务并添加超时支持
 */
template<typename T>
class timeout_wrapper {
private:
    enhanced_task<T> task_;
    std::chrono::milliseconds timeout_;
    cancellation_source timeout_source_;
    
public:
    timeout_wrapper(enhanced_task<T> task, std::chrono::milliseconds timeout)
        : task_(std::move(task)), timeout_(timeout) {}
    
    // 使包装器可等待
    struct awaiter {
        timeout_wrapper& wrapper_;
        std::optional<T> result_;
        std::exception_ptr exception_;
        std::atomic<bool> completed_{false};
        std::atomic<bool> timed_out_{false};
        
        bool await_ready() { return false; }
        
        void await_suspend(std::coroutine_handle<> waiting_handle) {
            auto token = wrapper_.timeout_source_.get_token();
            wrapper_.task_.set_cancellation_token(token);
            
            // 启动主任务（非协程版本，避免co_await问题）
            GlobalThreadPool::get().enqueue_void([
                this, waiting_handle, task_addr = wrapper_.task_.get_handle_address()
            ]() mutable {
                try {
                    // 直接调用任务的get()方法来获取结果
                    if constexpr (!std::is_void_v<T>) {
                        result_ = wrapper_.task_.get();
                    } else {
                        wrapper_.task_.get();
                    }
                    
                    bool expected = false;
                    if (completed_.compare_exchange_strong(expected, true)) {
                        waiting_handle.resume();
                    }
                } catch (...) {
                    exception_ = std::current_exception();
                    bool expected = false;
                    if (completed_.compare_exchange_strong(expected, true)) {
                        waiting_handle.resume();
                    }
                }
            });
            
            // 启动超时计时器
            GlobalThreadPool::get().enqueue_void([
                this, waiting_handle, timeout = wrapper_.timeout_
            ]() {
                std::this_thread::sleep_for(timeout);
                
                bool expected = false;
                if (completed_.compare_exchange_strong(expected, true)) {
                    timed_out_.store(true);
                    wrapper_.timeout_source_.cancel();
                    
                    exception_ = std::make_exception_ptr(
                        std::runtime_error("Operation timed out after " + 
                                         std::to_string(timeout.count()) + "ms")
                    );
                    waiting_handle.resume();
                }
            });
        }
        
        T await_resume() {
            if (timed_out_.load()) {
                LOG_ERROR("timeout_wrapper: Operation timed out");
            }
            
            if (exception_) {
                std::rethrow_exception(exception_);
            }
            
            if constexpr (!std::is_void_v<T>) {
                if (result_) {
                    return std::move(result_.value());
                } else {
                    throw std::runtime_error("Task completed without result");
                }
            }
        }
    };
    
    awaiter operator co_await() {
        return awaiter{*this};
    }
    
    // 检查是否超时
    bool is_timed_out() const {
        return timeout_source_.is_cancelled();
    }
    
    // 手动取消
    void cancel() {
        timeout_source_.cancel();
    }
};

// 便利函数
template<typename T>
auto with_timeout(enhanced_task<T> task, std::chrono::milliseconds timeout) {
    return timeout_wrapper<T>{std::move(task), timeout};
}

template<typename T>
auto with_timeout(enhanced_task<T> task, std::chrono::seconds timeout) {
    return with_timeout(std::move(task), 
                       std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
}

/**
 * @brief RAII协程生命周期守护
 * 
 * 自动管理协程的生命周期统计和清理
 */
class coroutine_guard {
private:
    bool registered_;
    
public:
    coroutine_guard() : registered_(true) {
        coroutine_lifecycle_manager::get().on_coroutine_created();
    }
    
    ~coroutine_guard() {
        if (registered_) {
            coroutine_lifecycle_manager::get().on_coroutine_completed();
        }
    }
    
    // 移动构造
    coroutine_guard(coroutine_guard&& other) noexcept 
        : registered_(std::exchange(other.registered_, false)) {}
    
    coroutine_guard& operator=(coroutine_guard&& other) noexcept {
        if (this != &other) {
            registered_ = std::exchange(other.registered_, false);
        }
        return *this;
    }
    
    // 禁止拷贝
    coroutine_guard(const coroutine_guard&) = delete;
    coroutine_guard& operator=(const coroutine_guard&) = delete;
    
    // 手动标记为取消或失败
    void mark_cancelled() {
        if (registered_) {
            coroutine_lifecycle_manager::get().on_coroutine_cancelled();
            registered_ = false;
        }
    }
    
    void mark_failed() {
        if (registered_) {
            coroutine_lifecycle_manager::get().on_coroutine_failed();
            registered_ = false;
        }
    }
};

} // namespace flowcoro::lifecycle

// 为了便于使用，在主命名空间中提供常用别名
namespace flowcoro {
    
// 生命周期管理器别名
using lifecycle_manager = lifecycle::coroutine_lifecycle_manager;
using lifecycle_stats = lifecycle::coroutine_lifecycle_manager::stats;

// 取消机制别名
using cancellation_token = lifecycle::cancellation_token;
using cancellation_source = lifecycle::cancellation_source;
using operation_cancelled = lifecycle::operation_cancelled_exception;

// 便利函数
template<typename T>
auto with_timeout(T&& task, std::chrono::milliseconds timeout) {
    return lifecycle::with_timeout(std::forward<T>(task), timeout);
}

template<typename T>
auto with_timeout(T&& task, std::chrono::seconds timeout) {
    return lifecycle::with_timeout(std::forward<T>(task), timeout);
}

// 全局便利函数
inline auto get_coroutine_stats() {
    return lifecycle_manager::get().get_stats();
}

inline void cancel_all_coroutines() {
    lifecycle_manager::get().cancel_all();
}

} // namespace flowcoro
