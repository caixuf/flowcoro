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
        std::atomic<bool> has_error{false}; // 使用原子布尔确保线程安全
        std::exception_ptr exception_; // 保存异常（exception_ptr本身是线程安全的）
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
            if (has_error.load(std::memory_order_acquire)) {
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
        std::suspend_never initial_suspend() noexcept { return {}; }  // 同步执行直到首个挂起点
        
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
            // 使用更快的路径，减少分支预测失误
            if (__builtin_expect(!is_cancelled_.load(std::memory_order_relaxed), true)) [[likely]] {
                value = std::move(v);
            }
        }

        void unhandled_exception() {
            // 快速路径：使用原子操作设置错误标志，确保线程安全
            has_error.store(true, std::memory_order_release);
            exception_ = std::current_exception(); // 捕获异常（exception_ptr是线程安全的）
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

        // 快速获取错误状态 - 使用原子操作
        bool safe_has_error() const noexcept {
            return has_error.load(std::memory_order_acquire) && !is_destroyed_.load(std::memory_order_acquire);
        }

        // 获取异常 - 添加内存屏障确保可见性
        std::exception_ptr get_exception() const noexcept {
            // 确保先检查has_error标志（内存屏障）
            if (has_error.load(std::memory_order_acquire)) {
                return exception_;
            }
            return nullptr;
        }

        // 重新抛出异常 - 线程安全版本
        void rethrow_if_exception() const {
            // 使用原子操作检查错误标志，确保内存可见性
            if (has_error.load(std::memory_order_acquire) && exception_) {
                std::rethrow_exception(exception_);
            }
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

            try {
                // 先检查协程状态，再原子地设置销毁标志
                // 这避免了检查和销毁之间的竞态窗口
                bool is_done = handle.done();
                
                // 使用原子操作检查和设置销毁状态，避免重复销毁
                bool expected = false;
                if (!handle.promise().is_destroyed_.compare_exchange_strong(expected, true, std::memory_order_release)) {
                    // 已经在销毁中，直接返回
                    handle = nullptr;
                    return;
                }

                // 延迟销毁 - 避免在协程执行栈中销毁
                if (is_done) {
                    handle.destroy();
                } else {
                    // 安排在下一个调度周期销毁
                    manager.schedule_destroy(handle);
                }
            } catch (...) {
                // 忽略销毁过程中的异常
                LOG_ERROR("Exception during safe_destroy");
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

        // 关键修复：先检查是否已完成（suspend_never 情况下协程会同步执行直到挂起）
        if (handle.done()) {
            goto get_result;
        }

        // 只有在未完成时才进入调度逻辑
        if (!handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            
            // 🔧 修复：添加超时保护和正确的等待策略
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5); // 5秒超时
            
            auto wait_time = std::chrono::microseconds(1);
            const auto max_wait = std::chrono::microseconds(100);
            size_t spin_count = 0;
            const size_t max_spins = 100;
            
            while (!handle.done() && !handle.promise().is_cancelled()) {
                // 超时检查
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > timeout) {
                    LOG_ERROR("Task::get: Timeout after 5 seconds");
                    if constexpr (std::is_default_constructible_v<T>) {
                        return T{};
                    } else {
                        std::terminate();
                    }
                }
                
                // 驱动协程管理器
                manager.drive();
                
                // 自适应等待策略
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
        // 重新抛出异常（如果有）
        handle.promise().rethrow_if_exception();

        auto safe_value = handle.promise().safe_get_value();
        if (safe_value.has_value()) {
            return std::move(safe_value.value());
        } else {
            throw std::runtime_error("Task completed without setting a value");
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
            throw std::runtime_error("Task await_resume: Invalid handle");
        }

        if (handle.promise().is_destroyed()) {
            throw std::runtime_error("Task await_resume: Task destroyed");
        }

        // 重新抛出异常
        handle.promise().rethrow_if_exception();

        auto safe_value = handle.promise().safe_get_value();
        if (safe_value.has_value()) {
            return std::move(safe_value.value());
        } else {
            throw std::runtime_error("Task completed without setting a value");
        }
    }

    // 不抛异常版本（向后兼容）
    std::optional<T> try_get() noexcept {
        if (!handle || handle.promise().is_destroyed()) {
            return std::nullopt;
        }
        if (handle.promise().has_error) {
            return std::nullopt;
        }
        return handle.promise().safe_get_value();
    }

    // 获取错误信息
    std::optional<std::string> get_error_message() const noexcept {
        if (!handle || !handle.promise().exception_) {
            return std::nullopt;
        }
        try {
            std::rethrow_exception(handle.promise().exception_);
        } catch (const std::exception& e) {
            return std::string(e.what());
        } catch (...) {
            return std::string("Unknown exception");
        }
    }
};

} // namespace flowcoro

// 包含特化版本
#include "task_specializations.h"
