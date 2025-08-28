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

// 前向声明
class CoroutineManager;

// 支持返回值的Task - 整合SafeTask的RAII和异常安全特性
template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        bool has_error = false; // 替换exception_ptr
        std::coroutine_handle<> continuation; // 懒加载Task的continuation支持

        // 增强版生命周期管理 - 融合SafeCoroutineHandle概念
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;

        promise_type() : creation_time_(std::chrono::steady_clock::now()) {
            // 记录任务创建
            PerformanceMonitor::get_instance().on_task_created();
        }

        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
            
            // 记录任务状态
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
        std::suspend_never initial_suspend() noexcept { return {}; }  // 立即执行以提高性能
        
        // 支持continuation的final_suspend
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise;
                
                bool await_ready() const noexcept { return false; }
                
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    // 如果有continuation，恢复它
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
            // 快速路径：通常情况下协程没有被取消或销毁
            if (!is_cancelled_.load(std::memory_order_relaxed)) [[likely]] {
                value = std::move(v);
            }
        }

        void unhandled_exception() {
            // 快速路径：直接设置错误标志
            has_error = true;
            LOG_ERROR("Task unhandled exception occurred");
        }

        // Continuation支持
        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation = cont;
        }

        // 快速的取消支持 - 去除锁
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

        // 快速获取值 - 去除锁，使用原子读取
        std::optional<T> safe_get_value() const noexcept {
            // 快速路径：通常情况下协程没有被销毁
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                if constexpr (std::is_move_constructible_v<T>) {
                    if (value.has_value()) {
                        // 对于可移动类型，避免拷贝
                        return std::make_optional(std::move(const_cast<std::optional<T>&>(value).value()));
                    }
                } else {
                    return value;
                }
            }
            return std::nullopt;
        }

        // 快速获取错误状态 - 去除锁
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
            // 直接销毁当前句柄，避免递归调用safe_destroy
            if (handle) {
                if( handle.address() != nullptr && !handle.done() ) {
                    handle.promise().request_cancellation(); // 请求取消
                    safe_destroy(); // 安全销毁当前句柄
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
        // 增强版析构：使用安全销毁
        safe_destroy();
    }

    // 增强版：安全取消支持
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

    // 简化版：基本状态查询
    std::chrono::milliseconds get_lifetime() const {
        if (!handle) return std::chrono::milliseconds{0};
        return handle.promise().get_lifetime();
    }

    bool is_active() const {
        return handle && !handle.done() && !is_cancelled();
    }

    // JavaScript Promise 风格状态查询API
    bool is_pending() const noexcept {
        if (!handle) return false;
        return !handle.done() && !is_cancelled();
    }

    bool is_settled() const noexcept {
        if (!handle) return true; // 无效句柄视为已结束
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

    // 安全销毁方法 
    void safe_destroy() {
        if (handle && handle.address()) {
            auto& manager = CoroutineManager::get_instance();

            if (!handle.promise().is_destroyed()) {
                // 标记为销毁状态
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }

            // 延迟销毁 - 避免在协程执行栈中销毁
            if (handle.done()) {
                handle.destroy();
            } else {
                // 安排在下一个调度周期销毁
                manager.schedule_destroy(handle);
            }
            handle = nullptr;
        }
    }

    T get() {
        // 增强版：安全状态检查
        if (!handle) {
            LOG_ERROR("Task::get: Invalid handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        // 检查是否已销毁
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task::get: Task already destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }

        // 安全恢复协程 - 统一使用协程管理器调度
        if (!handle.done() && !handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            
            // 等待协程完成（优化的自适应等待）
            auto wait_time = std::chrono::nanoseconds(100); // 从100ns开始
            int spin_count = 0;
            const int max_spins = 1000; // 先自旋1000次
            const auto max_wait = std::chrono::nanoseconds(50000); // 50μs
            
            while (!handle.done() && !handle.promise().is_cancelled()) {
                manager.drive(); // 驱动协程池执行
                
                // 先进行无睡眠的快速轮询
                if (spin_count < max_spins) {
                    ++spin_count;
                    continue;
                }
                
                // 然后使用自适应睡眠
                std::this_thread::sleep_for(wait_time);
                wait_time = std::min(wait_time * 2, max_wait);
            }
        }

        // 检查是否有错误
        if (handle.promise().safe_has_error()) {
            // 不使用异常，记录错误日志并返回默认值
            LOG_ERROR("Task execution failed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                // 对于不可默认构造的类型，尝试使用value的移动构造
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
            LOG_ERROR("Task completed without setting a value");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate();
            }
        }
    }

    // SafeTask兼容方法：获取结果（同步）
    T get_result() requires(!std::is_void_v<T>) {
        return get(); // 复用现有的get()方法
    }

    void get_result() requires(std::is_void_v<T>) {
        get(); // 复用现有的get()方法
    }

    // SafeTask兼容方法：检查是否就绪
    bool is_ready() const noexcept {
        return await_ready();
    }

    // 使Task可等待 - 增强版安全检查
    bool await_ready() const {
        if (!handle) return true; // 无效句柄视为ready

        // 安全检查：验证句柄地址有效性
        if (!handle.address()) return true; // 无效地址视为ready
        if (handle.done()) return true; // 已完成视为ready

        // 只有在句柄有效时才检查promise状态
        return handle.promise().is_destroyed();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // 高性能实现：直接设置continuation
        if (!handle || handle.promise().is_destroyed()) {
            // 句柄无效，直接恢复等待协程
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        if (handle.done()) {
            // 任务已完成，直接恢复等待协程
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        // 设置continuation：当task完成时恢复waiting_handle
        handle.promise().set_continuation(waiting_handle);
    }

    T await_resume() {
        // 增强版：使用安全getter
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

// 包含特化版本
#include "task_specializations.h"
