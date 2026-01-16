#pragma once
#include <coroutine>
#include <optional>
#include <atomic>
#include <chrono>
#include <type_traits>
#include <thread>
#include <utility>
#include "performance_monitor.h"
#include "coroutine_manager.h"
#include "result.h"
#include "error_handling.h"
#include "logger.h"

namespace flowcoro {

// 
class CoroutineManager;

// Task - SafeTaskRAII
template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        bool has_error = false; // exception_ptr
        std::coroutine_handle<> continuation; // Taskcontinuation

        //  - SafeCoroutineHandle
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;

        promise_type() : creation_time_(std::chrono::steady_clock::now()) {
            // 
            PerformanceMonitor::get_instance().on_task_created();
        }

        // 
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
            
            // 
            if (has_error) {
                PerformanceMonitor::get_instance().on_task_failed();
            } else if (is_cancelled()) {
                PerformanceMonitor::get_instance().on_task_cancelled();
            } else if (value.has_value()) {
                PerformanceMonitor::get_instance().on_task_completed();
            }
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }  // 
        
        // continuationfinal_suspend
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise;
                
                bool await_ready() const noexcept { return false; }
                
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    // continuation
                    if (promise->continuation) {
                        return promise->continuation;
                    }
                    return std::noop_coroutine();
                }
                
                void await_resume() const noexcept {}
            };
            return final_awaiter{this};
        }

        void return_value(T v) noexcept {
            // 
            if (__builtin_expect(!is_cancelled_.load(std::memory_order_relaxed), true)) [[likely]] {
                value = std::move(v);
            }
        }

        void unhandled_exception() {
            // 
            has_error = true;
            LOG_ERROR("Task unhandled exception occurred");
        }

        // Continuation
        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation = cont;
        }

        //  - 
        void request_cancellation() noexcept {
            is_cancelled_.store(true, std::memory_order_release);
        }

        bool is_cancelled() const {
            return is_cancelled_.load(std::memory_order_acquire);
        }

        bool is_destroyed() const {
            return is_destroyed_.load(std::memory_order_acquire);
        }

        std::chrono::milliseconds get_lifetime() const {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_);
        }

        //  - 
        std::optional<T> safe_get_value() const noexcept {
            // 
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                if constexpr (std::is_move_constructible_v<T>) {
                    if (value.has_value()) {
                        // 
                        return std::make_optional(std::move(const_cast<std::optional<T>&>(value).value()));
                    }
                } else {
                    return value;
                }
            }
            return std::nullopt;
        }

        //  - 
        bool safe_has_error() const noexcept {
            return has_error && !is_destroyed_.load(std::memory_order_acquire);
        }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            // safe_destroy
            if (handle) {
                if( handle.address() != nullptr && !handle.done() ) {
                    handle.promise().request_cancellation(); // 
                    safe_destroy(); // 
                }
                else
                {
                    LOG_ERROR("Task::operator=: Attempting to destroy an already done or null handle");
                }
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Task() {
        // 
        safe_destroy();
    }

    // 
    void cancel() {
        if (handle && !handle.done() && !handle.promise().is_destroyed()) {
            handle.promise().request_cancellation();
            LOG_INFO("Task::cancel: Task cancelled (lifetime: %lld ms)",
                     handle.promise().get_lifetime().count());
        }
    }

    bool is_cancelled() const {
        if (!handle || handle.promise().is_destroyed()) return false;
        return handle.promise().is_cancelled();
    }

    // 
    std::chrono::milliseconds get_lifetime() const {
        if (!handle) return std::chrono::milliseconds{0};
        return handle.promise().get_lifetime();
    }

    bool is_active() const {
        return handle && !handle.done() && !is_cancelled();
    }

    // JavaScript Promise API
    bool is_pending() const noexcept {
        if (!handle) return false;
        return !handle.done() && !is_cancelled();
    }

    bool is_settled() const noexcept {
        if (!handle) return true; // 
        return handle.done() || is_cancelled();
    }

    bool is_fulfilled() const noexcept {
        if (!handle) return false;
        return handle.done() && !is_cancelled() && !handle.promise().has_error;
    }

    bool is_rejected() const noexcept {
        if (!handle) return false;
        return is_cancelled() || handle.promise().has_error;
    }

    //  
    void safe_destroy() {
        if (handle && handle.address()) {
            auto& manager = CoroutineManager::get_instance();

            // 
            bool expected = false;
            if (!handle.promise().is_destroyed_.compare_exchange_strong(expected, true, std::memory_order_release)) {
                // 
                handle = nullptr;
                return;
            }

            try {
                //  - 
                if (handle.done()) {
                    handle.destroy();
                } else {
                    // 
                    manager.schedule_destroy(handle);
                }
            } catch (...) {
                // 
                LOG_ERROR("Exception during safe_destroy");
            }
            handle = nullptr;
        }
    }

    T get() {
        // 
        if (!handle) {
            LOG_ERROR("Task::get: Invalid handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        // 
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task::get: Task already destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        //  suspend_never 
        if (handle.done()) {
            goto get_result;
        }

        // 
        if (!handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            
            //  
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5); // 5
            
            auto wait_time = std::chrono::microseconds(1);
            const auto max_wait = std::chrono::microseconds(100);
            size_t spin_count = 0;
            const size_t max_spins = 100;
            
            while (!handle.done() && !handle.promise().is_cancelled()) {
                // 
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > timeout) {
                    LOG_ERROR("Task::get: Timeout after 5 seconds");
                    if constexpr (std::is_default_constructible_v<T>) {
                        return T{};
                    } else {
                        std::terminate();
                    }
                }
                
                // 
                manager.drive();
                
                // 
                if (spin_count < max_spins) {
                    ++spin_count;
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(wait_time);
                    wait_time = std::min(wait_time * 2, max_wait);
                }
            }
        }

get_result:
        // 
        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task execution failed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                auto safe_value = handle.promise().safe_get_value();
                if (safe_value.has_value()) {
                    return std::move(safe_value.value());
                }
                std::terminate();
            }
        }

        auto safe_value = handle.promise().safe_get_value();
        if (safe_value.has_value()) {
            return std::move(safe_value.value());
        } else {
            LOG_ERROR("Task completed without setting a value");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }
    }

    // SafeTask
    T get_result() requires(!std::is_void_v<T>) {
        return get(); // get()
    }

    void get_result() requires(std::is_void_v<T>) {
        get(); // get()
    }

    // SafeTask
    bool is_ready() const noexcept {
        return await_ready();
    }

    // Task - 
    bool await_ready() const {
        if (!handle) return true; // ready

        // 
        if (!handle.address()) return true; // ready
        if (handle.done()) return true; // ready

        // promise
        return handle.promise().is_destroyed();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // continuation
        if (!handle || handle.promise().is_destroyed()) {
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        if (handle.done()) {
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        // continuationtaskwaiting_handle
        handle.promise().set_continuation(waiting_handle);
    }

    T await_resume() {
        // getter
        if (!handle) {
            LOG_ERROR("Task await_resume: Invalid handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task await_resume: Task destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task await_resume: error occurred");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                auto safe_value = handle.promise().safe_get_value();
                if (safe_value.has_value()) {
                    return std::move(safe_value.value());
                }
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        auto safe_value = handle.promise().safe_get_value();
        if (safe_value.has_value()) {
            return std::move(safe_value.value());
        } else {
            LOG_ERROR("Task await_resume: no value set");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }
    }
};

} // namespace flowcoro

// 
#include "task_specializations.h"
