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
#include "cancellation.h"

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
#ifndef NDEBUG
        // [Perf优化] creation_time_ 仅用于诊断（get_lifetime），Release 下关掉
        // callgrind: steady_clock::now() 每个 Task 构造都触发，2000 任务就是 2000 次 vDSO 调用
        std::chrono::steady_clock::time_point creation_time_;
#endif

        promise_type()
#ifndef NDEBUG
            : creation_time_(std::chrono::steady_clock::now())
#endif
        {
#ifndef NDEBUG
            // [Perf优化] PerformanceMonitor::get_instance() 是 static local singleton，
            // 每次构造/析构都访问，在热路径上产生不必要的原子读。Release 下禁用。
            PerformanceMonitor::get_instance().on_task_created();
#endif
        }

        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
#ifndef NDEBUG
            // 记录任务状态（仅 Debug/RelWithDebInfo 构建时启用）
            if (has_error.load(std::memory_order_acquire)) {
                PerformanceMonitor::get_instance().on_task_failed();
            } else if (is_cancelled()) {
                PerformanceMonitor::get_instance().on_task_cancelled();
            } else if (value.has_value()) {
                PerformanceMonitor::get_instance().on_task_completed();
            }
#endif
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

        // 将外部 CancellationToken 绑定到本 Task：token 取消时自动调用 request_cancellation
        // 若 token 已取消，则立即同步取消
        void attach_cancellation_token(const CancellationToken& token) noexcept {
            if (token.is_cancelled()) {
                request_cancellation();
                return;
            }
            // 注意：this 指向 promise，promise 的生命周期由协程帧决定。
            // 协程帧在 safe_destroy 时才销毁，此时回调链表里的 ctx 已不再有效。
            // 为避免悬空指针，仅当 token 早于协程销毁时才会触发回调；
            // 若协程先被销毁，回调中再次访问 promise 是 UB。
            // 当前实现仅作为 "尽力而为" 的取消传播：要求 token 的生命周期
            // 不长于协程。完整方案需要 weak handle，这里满足基本用例。
            token.register_callback([](void* self) {
                auto* promise = static_cast<promise_type*>(self);
                promise->request_cancellation();
            }, this);
        }

        bool is_destroyed() const {
            return is_destroyed_.load(std::memory_order_acquire);
        }

        std::chrono::milliseconds get_lifetime() const {
#ifndef NDEBUG
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_);
#else
            return std::chrono::milliseconds{0};
#endif
        }

        // 快速获取值 - 去除锁，使用原子读取
        std::optional<T> safe_get_value() const noexcept {
            // 快速路径：通常情况下协程没有被销毁
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                if (value.has_value()) {
                    // 返回值的拷贝，保持const语义正确
                    // 移除const_cast误用，因为这是const函数不应修改状态
                    return value;
                }
            }
            return std::nullopt;
        }

        // 快速获取错误状态 - 使用原子操作
        bool safe_has_error() const noexcept {
            return has_error.load(std::memory_order_acquire) && !is_destroyed_.load(std::memory_order_acquire);
        }

        // 获取异常 - 线程安全版本，确保内存可见性
        std::exception_ptr get_exception() const noexcept {
            // 原子检查has_error，然后在本地拷贝exception_ptr
            // exception_ptr本身的拷贝是线程安全的
            if (has_error.load(std::memory_order_acquire)) {
                // 拷贝exception_ptr到本地变量，避免TOCTOU
                std::exception_ptr local_exception = exception_;
                return local_exception;
            }
            return nullptr;
        }

        // 重新抛出异常 - 线程安全版本
        void rethrow_if_exception() const {
            // 获取异常的本地拷贝以避免竞态条件
            auto exc = get_exception();
            if (exc) {
                std::rethrow_exception(exc);
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

    // 放弃对协程帧的所有权：置空 handle，析构时不调用 safe_destroy。
    // 协程帧仍由 CoroutineManager 管理，会在进程退出或定期清理时回收。
    // 用于 when_any 等场景：selector Task 可能在不同于协程执行线程的
    // 地方被析构，调用 safe_destroy → done() 会触发跨线程读取。
    void detach() noexcept {
        handle = nullptr;
    }

    // 增强版：安全取消支持
    void cancel() {
        if (handle && !handle.done() && !handle.promise().is_destroyed()) {
            handle.promise().request_cancellation();
            LOG_INFO("Task::cancel: Task cancelled (lifetime: %lld ms)",
                     handle.promise().get_lifetime().count());
        }
    }

    // 将外部 CancellationToken 绑定到本 Task：token 取消时本 Task 自动取消
    void attach_cancellation_token(const CancellationToken& token) noexcept {
        if (handle && !handle.promise().is_destroyed()) {
            handle.promise().attach_cancellation_token(token);
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
                // 使用原子操作检查和设置销毁状态，避免重复销毁
                bool expected = false;
                if (!handle.promise().is_destroyed_.compare_exchange_strong(expected, true, std::memory_order_release)) {
                    // 已经在销毁中，直接返回
                    handle = nullptr;
                    return;
                }

                // 统一用延迟销毁：不在此处调用 handle.done()。
                // 原因：GCC 的 coroutine_handle::done() 会读取协程帧内部的
                // __resume_index 字段，该字段在协程 final_suspend 时被写入。
                // 当 Task 在不同于协程执行线程的地方被析构时（例如 when_any
                // 的 selector Task 在 caller 线程析构，而 selector 协程在
                // 工作线程执行），done() 的跨线程读取是 data race（TSAN 报警）。
                // schedule_destroy 把帧入队，由 CoroutineManager 在安全线程
                // 调用 process_destroy_queue 处理；process_destroy_queue 会在
                // 销毁前检查 done()，未完成的协程会被跳过等下一轮。
                manager.schedule_destroy(handle);
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
                throw std::runtime_error("Task::get called on invalid handle with non-default-constructible type");
            }
        }

        // 检查是否已销毁
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task::get: Task already destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                throw std::runtime_error("Task::get called on destroyed task with non-default-constructible type");
            }
        }

        // 关键修复：先检查是否已完成（suspend_never 情况下协程会同步执行直到挂起）
        if (handle.done()) {
            goto get_result;
        }

        // 只有在未完成时才进入调度逻辑
        if (!handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            
            // 🔧 修复：不直接调度外层句柄，通过驱动管理器让子任务完成并触发延续链
            // 直接调度外层句柄会导致在子任务尚未完成时提前调用 await_resume，引发异常
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
                        throw std::runtime_error("Task::get timed out after 5 seconds");
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
            // 简化错误日志，避免不必要的字符串分配
            LOG_ERROR("Task await_resume: No value available");
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
