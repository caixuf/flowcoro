#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <utility>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <future>
#include <tuple>
#include <optional>
#include <type_traits>
#include <iostream>
#include <vector>

namespace flowcoro {

// 简化的日志宏
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl  
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

// 协程状态枚举
enum class coroutine_state {
    created,
    running,
    suspended,
    completed,
    cancelled,
    destroyed,
    error
};

// 协程状态管理器
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};
    
public:
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        return state_.compare_exchange_strong(from, to);
    }
    
    void force_transition(coroutine_state to) noexcept {
        state_.store(to, std::memory_order_release);
    }
    
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
};

// 取消状态
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    std::mutex callbacks_mutex_;
    std::vector<std::function<void()>> callbacks_;
    
public:
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            for (auto& callback : callbacks_) {
                try { callback(); } catch (...) {}
            }
        }
    }
    
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    template<typename Callback>
    void register_callback(Callback&& cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.emplace_back(std::forward<Callback>(cb));
        
        if (is_cancelled()) {
            try { cb(); } catch (...) {}
        }
    }
    
    void clear_callbacks() noexcept {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.clear();
    }
};

// 安全协程句柄
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> valid_{false};
    
public:
    safe_coroutine_handle() = default;
    
    template<typename Promise>
    explicit safe_coroutine_handle(std::coroutine_handle<Promise> h)
        : handle_(h), valid_(h && !h.done()) {}
    
    explicit safe_coroutine_handle(std::coroutine_handle<> h)
        : handle_(h), valid_(h && !h.done()) {}
    
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept
        : handle_(std::exchange(other.handle_, {}))
        , valid_(other.valid_.exchange(false)) {}
    
    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept {
        if (this != &other) {
            handle_ = std::exchange(other.handle_, {});
            valid_.store(other.valid_.exchange(false));
        }
        return *this;
    }
    
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    ~safe_coroutine_handle() {
        if (valid() && handle_) {
            try { handle_.destroy(); } catch (...) {}
        }
    }
    
    bool valid() const noexcept {
        return valid_.load(std::memory_order_acquire) && handle_;
    }
    
    bool done() const noexcept {
        return !valid() || handle_.done();
    }
    
    void resume() {
        if (valid() && !handle_.done()) {
            handle_.resume();
        }
    }
    
    template<typename Promise>
    Promise& promise() {
        if (!valid()) {
            throw std::runtime_error("Invalid coroutine handle");
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }
    
    void invalidate() noexcept {
        valid_.store(false, std::memory_order_release);
    }
    
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
};

// 简化版Task
template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_value(T v) noexcept { 
            value = std::move(v);
        }
        
        void unhandled_exception() { 
            exception = std::current_exception();
        }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Task() { 
        if (handle) {
            try {
                if (handle.address() != nullptr) {
                    handle.destroy();
                }
            } catch (...) {}
        }
    }
    
    T get() {
        if (handle && !handle.done()) handle.resume();
        if (handle.promise().exception) {
            LOG_ERROR("Task execution failed with exception");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                if (handle.promise().value.has_value()) {
                    return std::move(handle.promise().value.value());
                }
                std::terminate();
            }
        }
        if (handle.promise().value.has_value()) {
            return std::move(handle.promise().value.value());
        } else {
            LOG_ERROR("Task completed without setting a value");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
    }
    
    bool await_ready() const {
        return handle && handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // 简化版：直接在当前线程执行
        if (handle && !handle.done()) {
            handle.resume();
        }
        if (waiting_handle && !waiting_handle.done()) {
            waiting_handle.resume();
        }
    }
    
    T await_resume() {
        if (handle.promise().exception) {
            LOG_ERROR("Task await_resume: exception occurred");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                if (handle.promise().value.has_value()) {
                    return std::move(handle.promise().value.value());
                }
                std::terminate();
            }
        }
        if (handle.promise().value.has_value()) {
            return std::move(handle.promise().value.value());
        } else {
            LOG_ERROR("Task await_resume: no value set");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
    }
};

// Task<void>特化
template<>
struct Task<void> {
    struct promise_type {
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_void() noexcept {}
        void unhandled_exception() { 
            exception = std::current_exception();
        }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Task() { 
        if (handle) {
            try {
                if (handle.address() != nullptr) {
                    handle.destroy();
                }
            } catch (...) {}
        }
    }
    
    void get() {
        if (handle && !handle.done()) handle.resume();
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
    }
    
    bool await_ready() const {
        return handle && handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            handle.resume();
        }
        if (waiting_handle && !waiting_handle.done()) {
            waiting_handle.resume();
        }
    }
    
    void await_resume() {
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
    }
};

// 简化的sleep实现
class SleepAwaiter {
public:
    explicit SleepAwaiter(std::chrono::milliseconds duration) 
        : duration_(duration) {}
    
    bool await_ready() const noexcept { 
        return duration_.count() <= 0;
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        // 简化版：直接在当前线程睡眠
        std::this_thread::sleep_for(duration_);
        if (h && !h.done()) {
            h.resume();
        }
    }
    
    void await_resume() const noexcept {}
    
private:
    std::chrono::milliseconds duration_;
};

inline auto sleep_for(std::chrono::milliseconds duration) {
    return SleepAwaiter(duration);
}

// 同步等待
template<typename T>
T sync_wait(Task<T>&& task) {
    try {
        return task.get();
    } catch (...) {
        LOG_ERROR("sync_wait: Task execution failed");
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return T{};
        }
    }
}

inline void sync_wait(Task<void>&& task) {
    try {
        task.get();
    } catch (...) {
        LOG_ERROR("sync_wait: Task execution failed");
    }
}

} // namespace flowcoro
