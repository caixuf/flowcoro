#pragma once
#include "coroutine_pool.h"
#include "enhanced_task.h"
#include <coroutine>
#include <optional>

namespace flowcoro::lifecycle {

/**
 * @brief 池化协程任务
 * 基于enhanced_task，添加了自动池化生命周期管理
 */
template<typename T = void>
class pooled_task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;
    
    struct promise_type {
        std::optional<T> result;
        std::exception_ptr exception = nullptr;
        coroutine_lifecycle_guard lifecycle_guard;
        cancellation_token cancel_token;
        
        pooled_task get_return_object() {
            auto handle = handle_type::from_promise(*this);
            
            // 创建池化的生命周期守护
            lifecycle_guard = make_pooled_coroutine_guard(
                handle, "pooled_task");
            
            return pooled_task{handle};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        
        auto final_suspend() noexcept {
            struct awaiter {
                bool await_ready() const noexcept { return false; }
                void await_suspend(handle_type h) const noexcept {
                    // 协程完成，生命周期守护会自动处理池化回收
                }
                void await_resume() const noexcept {}
            };
            return awaiter{};
        }
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
        
        // 对于void特化
        void return_void() requires std::is_void_v<T> {
            // 空实现
        }
        
        // 对于非void类型
        void return_value(T&& value) requires (!std::is_void_v<T>) {
            result = std::forward<T>(value);
        }
        
        void return_value(const T& value) requires (!std::is_void_v<T>) {
            result = value;
        }
        
        // 取消支持
        void set_cancellation_token(cancellation_token token) {
            cancel_token = std::move(token);
            lifecycle_guard.set_cancellation_token(cancel_token);
        }
        
        bool is_cancelled() const {
            return cancel_token.is_cancelled();
        }
    };

private:
    handle_type handle_;
    
public:
    explicit pooled_task(handle_type h) : handle_(h) {}
    
    // 移动语义
    pooled_task(pooled_task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}
        
    pooled_task& operator=(pooled_task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    
    // 禁止拷贝
    pooled_task(const pooled_task&) = delete;
    pooled_task& operator=(const pooled_task&) = delete;
    
    ~pooled_task() {
        if (handle_) {
            // 协程句柄的析构会触发生命周期守护的析构
            // 生命周期守护会处理池化回收
            handle_.destroy();
        }
    }
    
    // 等待协程完成
    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) const {
        // 简化的等待实现
        // 实际实现中需要更复杂的调度逻辑
    }
    
    T await_resume() {
        if (!handle_) {
            throw std::runtime_error("Invalid pooled task handle");
        }
        
        auto& promise = handle_.promise();
        
        if (promise.exception) {
            std::rethrow_exception(promise.exception);
        }
        
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            if (!promise.result) {
                throw std::runtime_error("Pooled task has no result");
            }
            return std::move(*promise.result);
        }
    }
    
    // 状态检查
    bool done() const noexcept {
        return !handle_ || handle_.done();
    }
    
    bool valid() const noexcept {
        return handle_.operator bool();
    }
    
    // 取消操作
    void set_cancellation_token(cancellation_token token) {
        if (handle_) {
            handle_.promise().set_cancellation_token(std::move(token));
        }
    }
    
    void cancel() {
        if (handle_) {
            auto& guard = handle_.promise().lifecycle_guard;
            guard.cancel();
        }
    }
    
    bool is_cancelled() const {
        return handle_ && handle_.promise().is_cancelled();
    }
    
    // 调试信息
    std::string get_debug_info() const {
        if (!handle_) {
            return "pooled_task: invalid";
        }
        
        auto& guard = handle_.promise().lifecycle_guard;
        
        std::ostringstream oss;
        oss << "pooled_task: "
            << "state=" << static_cast<int>(guard.get_state()) << ", "
            << "lifetime=" << guard.get_lifetime().count() << "ms, "
            << "done=" << (handle_.done() ? "true" : "false");
        
        return oss.str();
    }
};

/**
 * @brief 创建池化任务的便利函数
 */
template<typename Awaitable>
auto make_pooled_task(Awaitable&& awaitable) -> pooled_task<void> {
    co_await std::forward<Awaitable>(awaitable);
}

template<typename T, typename Awaitable>
auto make_pooled_task_with_result(Awaitable&& awaitable) -> pooled_task<T> {
    co_return co_await std::forward<Awaitable>(awaitable);
}

} // namespace flowcoro::lifecycle
