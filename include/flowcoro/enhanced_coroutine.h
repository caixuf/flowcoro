#pragma once

#include <coroutine>
#include <atomic>
#include <memory>
#include <optional>
#include <chrono>
#include <exception>
#include <functional>

namespace flowcoro::detail {

// 协程生命周期状态枚举
enum class coroutine_state {
    created,    // 刚创建
    running,    // 运行中
    suspended,  // 已挂起
    completed,  // 已完成
    destroyed,  // 已销毁
    cancelled   // 已取消
};

// 安全的协程句柄包装器
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> destroyed_{false};
    
public:
    safe_coroutine_handle() = default;
    
    explicit safe_coroutine_handle(std::coroutine_handle<> h) noexcept 
        : handle_(h) {}
    
    ~safe_coroutine_handle() {
        destroy();
    }
    
    // 移动构造和赋值
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept 
        : handle_(std::exchange(other.handle_, {}))
        , destroyed_(other.destroyed_.exchange(false)) {}
    
    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
            destroyed_.store(other.destroyed_.exchange(false));
        }
        return *this;
    }
    
    // 禁止拷贝
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    bool valid() const noexcept {
        return handle_ && !destroyed_.load(std::memory_order_acquire);
    }
    
    void resume() {
        if (valid() && !handle_.done()) {
            handle_.resume();
        }
    }
    
    bool done() const noexcept {
        return !handle_ || destroyed_.load(std::memory_order_acquire) || handle_.done();
    }
    
    void destroy() noexcept {
        bool expected = false;
        if (destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            if (handle_) {
                try {
                    handle_.destroy();
                } catch (...) {
                    // 记录但不重新抛出析构函数中的异常
                }
                handle_ = {};
            }
        }
    }
    
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
    
    template<typename Promise>
    Promise& promise() {
        if (!handle_) {
            throw std::runtime_error("Invalid coroutine handle");
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }
};

// 协程状态管理器
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};
    std::chrono::steady_clock::time_point creation_time_;
    std::atomic<std::chrono::steady_clock::time_point*> completion_time_{nullptr};
    
public:
    coroutine_state_manager() : creation_time_(std::chrono::steady_clock::now()) {}
    
    ~coroutine_state_manager() {
        auto* time_ptr = completion_time_.load();
        delete time_ptr;
    }
    
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        auto result = state_.compare_exchange_strong(from, to, std::memory_order_acq_rel);
        if (result && (to >= coroutine_state::completed)) {
            mark_completion();
        }
        return result;
    }
    
    void force_transition(coroutine_state to) noexcept {
        if (to >= coroutine_state::completed) {
            mark_completion();
        }
        state_.store(to, std::memory_order_release);
    }
    
    std::chrono::milliseconds get_lifetime() const {
        auto end = get_completion_time_or_now();
        auto duration = end - creation_time_;
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    }
    
    bool is_active() const noexcept {
        auto s = get_state();
        return s == coroutine_state::running || s == coroutine_state::suspended;
    }

private:
    void mark_completion() noexcept {
        auto* expected = completion_time_.load();
        if (expected == nullptr) {
            auto* new_time = new(std::nothrow) std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::now()
            );
            if (new_time && !completion_time_.compare_exchange_strong(expected, new_time)) {
                delete new_time;  // 另一个线程已经设置了完成时间
            }
        }
    }
    
    std::chrono::steady_clock::time_point get_completion_time_or_now() const {
        auto* time_ptr = completion_time_.load();
        return time_ptr ? *time_ptr : std::chrono::steady_clock::now();
    }
};

// 取消状态管理
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    mutable std::mutex callbacks_mutex_;
    std::vector<std::function<void()>> callbacks_;
    
public:
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // 首次取消，执行所有回调
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            for (auto& callback : callbacks_) {
                try {
                    callback();
                } catch (...) {
                    // 忽略回调异常
                }
            }
            callbacks_.clear();
        }
    }
    
    template<typename Callback>
    void register_callback(Callback&& cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        if (is_cancelled()) {
            // 已经取消，立即执行回调
            try {
                cb();
            } catch (...) {
                // 忽略异常
            }
        } else {
            callbacks_.emplace_back(std::forward<Callback>(cb));
        }
    }
};

} // namespace flowcoro::detail

namespace flowcoro {

// 操作取消异常
class operation_cancelled_exception : public std::exception {
public:
    const char* what() const noexcept override {
        return "Operation was cancelled";
    }
};

// 超时异常
class timeout_exception : public std::exception {
private:
    std::string message_;
    
public:
    explicit timeout_exception(const std::string& msg) : message_(msg) {}
    
    const char* what() const noexcept override {
        return message_.c_str();
    }
};

// 取消令牌
class cancellation_token {
private:
    std::shared_ptr<detail::cancellation_state> state_;
    
public:
    cancellation_token() = default;
    
    explicit cancellation_token(std::shared_ptr<detail::cancellation_state> state)
        : state_(std::move(state)) {}
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw operation_cancelled_exception{};
        }
    }
    
    template<typename Callback>
    void register_callback(Callback&& cb) const {
        if (state_) {
            state_->register_callback(std::forward<Callback>(cb));
        }
    }
};

// 取消源
class cancellation_source {
private:
    std::shared_ptr<detail::cancellation_state> state_;
    
public:
    cancellation_source() 
        : state_(std::make_shared<detail::cancellation_state>()) {}
    
    cancellation_token get_token() const {
        return cancellation_token{state_};
    }
    
    void cancel() noexcept {
        if (state_) {
            state_->request_cancellation();
        }
    }
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
};

// 改进的Task实现（示例框架）
template<typename T>
class enhanced_task {
public:
    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr exception_;
        detail::coroutine_state_manager* state_manager_ = nullptr;
        cancellation_token cancel_token_;
        
        enhanced_task get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return enhanced_task{detail::safe_coroutine_handle{handle}};
        }
        
        std::suspend_never initial_suspend() noexcept {
            if (state_manager_) {
                state_manager_->try_transition(
                    detail::coroutine_state::created,
                    detail::coroutine_state::running
                );
            }
            return {};
        }
        
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise_;
                
                constexpr bool await_ready() noexcept { return false; }
                
                void await_suspend(std::coroutine_handle<promise_type>) noexcept {
                    if (promise_->state_manager_) {
                        promise_->state_manager_->force_transition(
                            detail::coroutine_state::completed
                        );
                    }
                }
                
                constexpr void await_resume() noexcept {}
            };
            
            return final_awaiter{this};
        }
        
        void return_value(T value) {
            cancel_token_.throw_if_cancelled();
            if constexpr (std::is_move_constructible_v<T>) {
                value_ = std::move(value);
            } else {
                value_ = value;
            }
        }
        
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
            if (state_manager_) {
                state_manager_->force_transition(detail::coroutine_state::destroyed);
            }
        }
    };

private:
    detail::safe_coroutine_handle handle_;
    detail::coroutine_state_manager state_manager_;
    cancellation_source cancel_source_;
    
public:
    explicit enhanced_task(detail::safe_coroutine_handle handle)
        : handle_(std::move(handle)) {
        if (handle_.valid()) {
            auto& promise = handle_.template promise<promise_type>();
            promise.state_manager_ = &state_manager_;
            promise.cancel_token_ = cancel_source_.get_token();
        }
    }
    
    // 移动语义
    enhanced_task(enhanced_task&&) = default;
    enhanced_task& operator=(enhanced_task&&) = default;
    
    // 禁止拷贝
    enhanced_task(const enhanced_task&) = delete;
    enhanced_task& operator=(const enhanced_task&) = delete;
    
    // 获取结果
    T get() {
        if (!handle_.valid()) {
            throw std::runtime_error("Invalid task handle");
        }
        
        // 等待协程完成
        while (handle_.valid() && !handle_.done()) {
            auto& promise = handle_.template promise<promise_type>();
            promise.cancel_token_.throw_if_cancelled();
            
            handle_.resume();
            std::this_thread::yield();
        }
        
        auto& promise = handle_.template promise<promise_type>();
        
        if (promise.exception_) {
            std::rethrow_exception(promise.exception_);
        }
        
        if (!promise.value_) {
            throw std::runtime_error("Task completed without value");
        }
        
        if constexpr (std::is_move_constructible_v<T>) {
            return std::move(promise.value_.value());
        } else {
            return promise.value_.value();
        }
    }
    
    // 取消和状态查询
    void cancel() {
        cancel_source_.cancel();
        state_manager_.force_transition(detail::coroutine_state::cancelled);
    }
    
    bool is_cancelled() const noexcept {
        return state_manager_.get_state() == detail::coroutine_state::cancelled ||
               cancel_source_.is_cancelled();
    }
    
    bool is_ready() const noexcept {
        return handle_.done();
    }
    
    detail::coroutine_state get_state() const noexcept {
        return state_manager_.get_state();
    }
    
    std::chrono::milliseconds get_lifetime() const {
        return state_manager_.get_lifetime();
    }
};

} // namespace flowcoro
