#pragma once

#include "core.h"
#include "cancellation.h" 
#include "../core.h"  // 为了使用GlobalThreadPool
#include "../logger.h"
#include <optional>
#include <exception>

namespace flowcoro::lifecycle {

// 增强的Task实现，保持与原接口兼容
template<typename T>
class enhanced_task {
public:
    // Promise类型，与原Task兼容
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        
        // 生命周期管理组件
        coroutine_state_manager state_manager;
        cancellation_token cancel_token{};
        safe_coroutine_handle* owner_handle{nullptr};
        
        enhanced_task get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return enhanced_task{safe_coroutine_handle{handle}};
        }
        
        std::suspend_never initial_suspend() noexcept { 
            // 状态转换：created -> running
            state_manager.try_transition(
                coroutine_state::created, 
                coroutine_state::running
            );
            return {}; 
        }
        
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise_;
                
                bool await_ready() noexcept { return false; }
                
                void await_suspend(std::coroutine_handle<promise_type>) noexcept {
                    // 状态转换：running -> completed
                    promise_->state_manager.force_transition(coroutine_state::completed);
                }
                
                void await_resume() noexcept {}
            };
            
            return final_awaiter{this};
        }
        
        void return_value(T val) noexcept(std::is_nothrow_move_constructible_v<T>) {
            // 检查取消状态
            if (cancel_token.is_cancelled()) {
                state_manager.force_transition(coroutine_state::cancelled);
                return;
            }
            
            try {
                value = std::move(val);
            } catch (...) {
                exception = std::current_exception();
                state_manager.force_transition(coroutine_state::destroyed);
            }
        }
        
        void unhandled_exception() { 
            exception = std::current_exception();
            state_manager.force_transition(coroutine_state::destroyed);
        }
        
        // 设置取消令牌
        void set_cancellation_token(cancellation_token token) {
            cancel_token = std::move(token);
        }
        
        // 设置所有者句柄（用于双向引用）
        void set_owner_handle(safe_coroutine_handle* handle) {
            owner_handle = handle;
        }
    };

private:
    safe_coroutine_handle handle_;
    
public:
    // 构造函数
    explicit enhanced_task(safe_coroutine_handle handle)
        : handle_(std::move(handle)) {
        
        if (handle_.valid()) {
            auto& promise = handle_.template promise<promise_type>();
            promise.set_owner_handle(&handle_);
        }
    }
    
    // 移动语义（与原Task兼容）
    enhanced_task(const enhanced_task&) = delete;
    enhanced_task& operator=(const enhanced_task&) = delete;
    
    enhanced_task(enhanced_task&& other) noexcept = default;
    enhanced_task& operator=(enhanced_task&& other) noexcept = default;
    
    // 析构函数：自动清理，比原Task更安全
    ~enhanced_task() = default;
    
    // 获取结果（与原Task接口兼容）
    T get() {
        if (!handle_.valid()) {
            LOG_ERROR("enhanced_task::get: Invalid task handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        // 等待协程完成
        while (handle_.valid() && !handle_.done()) {
            // 检查取消状态
            auto& promise = handle_.template promise<promise_type>();
            if (promise.cancel_token.is_cancelled()) {
                LOG_INFO("enhanced_task::get: Task was cancelled");
                promise.state_manager.force_transition(coroutine_state::cancelled);
                break;
            }
            
            handle_.resume();
            
            // 简单的让步，避免忙等
            std::this_thread::yield();
        }
        
        if (!handle_.valid()) {
            LOG_ERROR("enhanced_task::get: Handle became invalid during execution");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        auto& promise = handle_.template promise<promise_type>();
        
        // 处理异常（与原Task行为一致）
        if (promise.exception) {
            LOG_ERROR("enhanced_task::get: Task execution failed with exception");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                if (promise.value.has_value()) {
                    return std::move(promise.value.value());
                }
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }
        
        // 处理取消状态
        if (promise.state_manager.get_state() == coroutine_state::cancelled) {
            LOG_INFO("enhanced_task::get: Task was cancelled, returning default value");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                if (promise.value.has_value()) {
                    return std::move(promise.value.value());
                }
                std::terminate();
            }
        }
        
        // 返回值
        if (promise.value.has_value()) {
            return std::move(promise.value.value());
        } else {
            LOG_ERROR("enhanced_task::get: Task completed without setting a value");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
    }
    
    // 可等待接口（与原Task接口兼容）
    bool await_ready() const {
        return handle_.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle_.valid() && !handle_.done()) {
            // 在线程池中运行当前任务，然后恢复等待的协程
            // 注意：我们通过地址传递句柄，避免拷贝
            auto handle_addr = handle_.address();
            GlobalThreadPool::get().enqueue_void([
                handle_addr, 
                waiting_handle
            ]() mutable {
                try {
                    // 从地址重构句柄
                    auto handle = safe_coroutine_handle{std::coroutine_handle<>::from_address(handle_addr)};
                    if (handle.valid() && !handle.done()) {
                        handle.resume();
                    }
                } catch (...) {
                    LOG_ERROR("enhanced_task::await_suspend: Exception during task execution");
                }
                
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        } else {
            // 任务已完成，直接恢复等待的协程
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        }
    }
    
    T await_resume() {
        if (!handle_.valid()) {
            LOG_ERROR("enhanced_task::await_resume: Invalid handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        auto& promise = handle_.template promise<promise_type>();
        
        if (promise.exception) {
            LOG_ERROR("enhanced_task::await_resume: exception occurred");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                if (promise.value.has_value()) {
                    return std::move(promise.value.value());
                }
                std::terminate();
            }
        }
        
        if (promise.value.has_value()) {
            return std::move(promise.value.value());
        } else {
            LOG_ERROR("enhanced_task::await_resume: no value set");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
    }
    
    // 新增功能：取消支持
    void cancel() {
        if (handle_.valid()) {
            auto& promise = handle_.template promise<promise_type>();
            // 如果有取消源，请求取消
            promise.state_manager.force_transition(coroutine_state::cancelled);
            
            // 记录取消事件
            LOG_INFO("enhanced_task::cancel: Task cancelled (lifetime: %lld ms)", 
                     promise.state_manager.get_lifetime().count());
        }
    }
    
    bool is_cancelled() const {
        if (!handle_.valid()) return false;
        
        auto& promise = handle_.template promise<promise_type>();
        return promise.state_manager.get_state() == coroutine_state::cancelled ||
               promise.cancel_token.is_cancelled();
    }
    
    // 新增功能：状态查询
    bool is_ready() const {
        return handle_.done();
    }
    
    coroutine_state get_state() const {
        if (!handle_.valid()) return coroutine_state::destroyed;
        
        auto& promise = handle_.template promise<promise_type>();
        return promise.state_manager.get_state();
    }
    
    std::chrono::milliseconds get_lifetime() const {
        if (!handle_.valid()) return std::chrono::milliseconds{0};
        
        auto& promise = handle_.template promise<promise_type>();
        return promise.state_manager.get_lifetime();
    }
    
    bool is_active() const {
        if (!handle_.valid()) return false;
        
        auto& promise = handle_.template promise<promise_type>();
        return promise.state_manager.is_active();
    }
    
    // 新增功能：设置取消令牌
    void set_cancellation_token(cancellation_token token) {
        if (handle_.valid()) {
            auto& promise = handle_.template promise<promise_type>();
            promise.set_cancellation_token(std::move(token));
        }
    }
    
    // 调试信息
    void* get_handle_address() const {
        return handle_.address();
    }
    
    bool is_handle_valid() const {
        return handle_.valid();
    }
};

} // namespace flowcoro::lifecycle

// 为了保持兼容性，在flowcoro命名空间中提供别名
namespace flowcoro {

// 可以选择性地启用增强任务
#ifdef FLOWCORO_USE_ENHANCED_TASK
    template<typename T>
    using Task = lifecycle::enhanced_task<T>;
#endif

// 便利函数：创建带取消令牌的任务
template<typename T>
auto make_cancellable_task(lifecycle::enhanced_task<T> task, lifecycle::cancellation_token token) {
    task.set_cancellation_token(std::move(token));
    return task;
}

// 便利函数：创建带超时的任务
template<typename T>
auto make_timeout_task(lifecycle::enhanced_task<T> task, std::chrono::milliseconds timeout) {
    lifecycle::cancellation_source timeout_source;
    
    // 启动超时计时器
    GlobalThreadPool::get().enqueue_void([
        source = std::move(timeout_source), 
        timeout
    ]() mutable {
        std::this_thread::sleep_for(timeout);
        source.cancel();
    });
    
    task.set_cancellation_token(timeout_source.get_token());
    return task;
}

} // namespace flowcoro
