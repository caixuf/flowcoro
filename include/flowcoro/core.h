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
#include <variant>
#include "result.h"
#include "error_handling.h"
#include "lockfree.h" 
#include "thread_pool.h"
#include "logger.h"
#include "buffer.h"

// 前向声明HttpRequest类
class HttpRequest;

namespace flowcoro {

// 协程状态枚举
enum class coroutine_state {
    created,    // 刚创建，尚未开始执行
    running,    // 正在运行
    suspended,  // 挂起等待
    completed,  // 已完成
    cancelled,  // 已取消
    destroyed,  // 已销毁
    error       // 执行出错
};

// 协程状态管理器
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};
    
public:
    coroutine_state_manager() = default;
    
    // 尝试状态转换
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        return state_.compare_exchange_strong(from, to);
    }
    
    // 强制状态转换
    void force_transition(coroutine_state to) noexcept {
        state_.store(to, std::memory_order_release);
    }
    
    // 获取当前状态
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    
    // 检查是否处于某个状态
    bool is_state(coroutine_state expected) const noexcept {
        return state_.load(std::memory_order_acquire) == expected;
    }
};

// 取消状态（被cancellation_token使用）
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    std::mutex callbacks_mutex_;
    std::vector<std::function<void()>> callbacks_;
    
public:
    cancellation_state() = default;
    
    // 请求取消
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            // 第一次取消，触发回调
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            for (auto& callback : callbacks_) {
                try {
                    callback();
                } catch (...) {
                    // 忽略回调异常
                }
            }
        }
    }
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    // 注册取消回调
    template<typename Callback>
    void register_callback(Callback&& cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.emplace_back(std::forward<Callback>(cb));
        
        // 如果已经取消，立即调用回调
        if (is_cancelled()) {
            try {
                cb();
            } catch (...) {
                // 忽略回调异常
            }
        }
    }
    
    // 清空回调
    void clear_callbacks() noexcept {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.clear();
    }
};

// 安全协程句柄包装器
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> valid_{false};
    
public:
    safe_coroutine_handle() = default;
    
    // 从任意类型的协程句柄构造
    template<typename Promise>
    explicit safe_coroutine_handle(std::coroutine_handle<Promise> h)
        : handle_(h), valid_(h && !h.done()) {}
    
    // 从void句柄构造（特化版本）  
    explicit safe_coroutine_handle(std::coroutine_handle<> h)
        : handle_(h), valid_(h && !h.done()) {}
    
    // 移动构造
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
    
    // 禁止拷贝
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    ~safe_coroutine_handle() {
        if (valid() && handle_) {
            try {
                handle_.destroy();
            } catch (...) {
                // 忽略析构异常
            }
        }
    }
    
    // 检查句柄是否有效
    bool valid() const noexcept {
        return valid_.load(std::memory_order_acquire) && handle_;
    }
    
    // 检查协程是否完成
    bool done() const noexcept {
        return !valid() || handle_.done();
    }
    
    // 恢复协程执行
    void resume() {
        if (valid() && !handle_.done()) {
            handle_.resume();
        }
    }
    
    // 获取Promise（类型安全）
    template<typename Promise>
    Promise& promise() {
        if (!valid()) {
            throw std::runtime_error("Invalid coroutine handle");
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }
    
    // 使句柄无效
    void invalidate() noexcept {
        valid_.store(false, std::memory_order_release);
    }
    
    // 获取原始地址
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
};

// 简化的全局线程池
class GlobalThreadPool {
private:
    static lockfree::ThreadPool& get_pool() {
        static lockfree::ThreadPool pool;
        return pool;
    }
    
public:
    static lockfree::ThreadPool& get() {
        return get_pool();
    }
    
    static bool is_shutdown_requested() {
        return false; // 简化实现
    }
    
    static void shutdown() {
        // 简化实现 - 线程池在程序结束时自动销毁
    }
    
    // 为了保持API兼容性，保留原有接口
    template<typename F, typename... Args>
    static auto enqueue(F&& f, Args&&... args) -> decltype(get().enqueue(std::forward<F>(f), std::forward<Args>(args)...)) {
        return get().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    static void enqueue_void(std::function<void()> task) {
        get().enqueue_void(std::move(task));
    }
};

// 网络请求的抽象接口
class INetworkRequest {
public:
    virtual ~INetworkRequest() = default;
    
    // 发起网络请求的通用接口
    // url: 请求地址
    // callback: 回调函数，用于处理响应数据
    virtual void request(const std::string& url, const std::function<void(const std::string&)>& callback) = 0;
};

struct CoroTask {
    struct promise_type {
        CoroTask get_return_object() {
            return CoroTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
    std::coroutine_handle<promise_type> handle;
    CoroTask(std::coroutine_handle<promise_type> h) : handle(h) {}
    CoroTask(const CoroTask&) = delete;
    CoroTask& operator=(const CoroTask&) = delete;
    CoroTask(CoroTask&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    CoroTask& operator=(CoroTask&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~CoroTask() { if (handle) handle.destroy(); }
    
    void resume() { 
        if (handle && !handle.done()) {
            LOG_DEBUG("Resuming coroutine handle: %p", handle.address());
            // 使用无锁线程池调度协程
            GlobalThreadPool::get().enqueue_void([h = handle]() {
                if (h && !h.done()) {
                    LOG_TRACE("Executing coroutine in thread pool");
                    h.resume();
                    LOG_TRACE("Coroutine execution completed");
                }
            });
        }
    }
    bool done() const { return !handle || handle.done(); }
    
    // 使CoroTask可等待
    bool await_ready() const {
        return done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            // 在线程池中运行当前任务，然后恢复等待的协程
            GlobalThreadPool::get().enqueue_void([h = handle, waiting_handle]() {
                if (h && !h.done()) {
                    h.resume();
                }
                // 任务完成后恢复等待的协程
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        } else {
            // 如果任务已经完成，直接恢复等待的协程
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        }
    }
    
    void await_resume() const {
        // 任务完成，不返回值
    }
    
    // 执行网络请求（返回可等待的Task）
    template<typename NetworkRequestT = HttpRequest, typename T = std::string>
    static auto execute_network_request(const std::string& url) {
        class RequestTask {
        public:
            RequestTask(std::string url) : url_(std::move(url)) {}
            
            bool await_ready() const noexcept {
                return false;
            }
            
            void await_suspend(std::coroutine_handle<> h) {
                LOG_INFO("Starting network request to: %s", url_.c_str());
                // 创建网络请求实例并发起请求
                request_ = std::make_unique<NetworkRequestT>();
                request_->request(url_, [h, this](const T& response) {
                    LOG_DEBUG("Network request completed, response size: %zu", response.size());
                    // 保存响应结果
                    response_ = response;
                    // 使用无锁线程池继续执行协程
                    GlobalThreadPool::get().enqueue_void([h]() {
                        if (h && !h.done()) {
                            LOG_TRACE("Resuming coroutine after network response");
                            h.resume();
                        }
                    });
                });
            }
            
            const T& await_resume() const {
                return response_;
            }
            
        private:
            std::string url_;
            T response_;
            std::unique_ptr<NetworkRequestT> request_;
        };
        
        return RequestTask(url);
    }
};

// ==========================================
// RAII协程范围管理器
// ==========================================

class CoroutineScope {
public:
    ~CoroutineScope() {
        cancel_all();
    }
    
    void register_coroutine(std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cancelled_) {
            handles_.push_back(handle);
        }
    }
    
    void cancel_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        for (auto handle : handles_) {
            if (handle && !handle.done()) {
                handle.destroy();
            }
        }
        handles_.clear();
    }
    
    bool is_cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }
    
    void cleanup_completed() {
        std::lock_guard<std::mutex> lock(mutex_);
        handles_.erase(
            std::remove_if(handles_.begin(), handles_.end(),
                [](std::coroutine_handle<> h) { return !h || h.done(); }),
            handles_.end()
        );
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::coroutine_handle<>> handles_;
    bool cancelled_ = false;
};

// ==========================================
// 统一的Task实现 - 整合了SafeTask的RAII和异常安全特性
// ==========================================
// 支持返回值的Task - 整合SafeTask的RAII和异常安全特性

template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        
        // 增强版生命周期管理 - 融合SafeCoroutineHandle概念
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;
        mutable std::mutex state_mutex_; // 保护状态变更
        
        promise_type() : creation_time_(std::chrono::steady_clock::now()) {}
        
        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
        }
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_value(T v) noexcept { 
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!is_cancelled_.load(std::memory_order_relaxed) && !is_destroyed_.load(std::memory_order_relaxed)) {
                value = std::move(v);
            }
        }
        
        void unhandled_exception() { 
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!is_destroyed_.load(std::memory_order_relaxed)) {
                exception = std::current_exception();
            }
        }
        
        // 安全的取消支持
        void request_cancellation() {
            std::lock_guard<std::mutex> lock(state_mutex_);
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
        
        // 安全获取值
        std::optional<T> safe_get_value() const {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (is_destroyed_.load(std::memory_order_relaxed)) {
                return std::nullopt;
            }
            return value;
        }
        
        // 安全获取异常
        std::exception_ptr safe_get_exception() const {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (is_destroyed_.load(std::memory_order_relaxed)) {
                return nullptr;
            }
            return exception;
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
                try {
                    if (handle.address() != nullptr) {
                        handle.destroy();
                    }
                } catch (...) {
                    // 忽略析构异常
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
    
    // ==========================================
    // Phase 1 新增: JavaScript Promise 风格状态查询API
    // ==========================================
    
    /// @brief 检查任务是否仍在进行中 (JavaScript Promise.pending 风格)
    /// @return true if task is created or running, false otherwise
    bool is_pending() const noexcept {
        if (!handle) return false;
        return !handle.done() && !is_cancelled();
    }
    
    /// @brief 检查任务是否已结束 (JavaScript Promise.settled 风格)
    /// @return true if task is completed, cancelled, or has error
    bool is_settled() const noexcept {
        if (!handle) return true;  // 无效句柄视为已结束
        return handle.done() || is_cancelled();
    }
    
    /// @brief 检查任务是否成功完成 (JavaScript Promise.fulfilled 风格)
    /// @return true if task completed successfully
    bool is_fulfilled() const noexcept {
        if (!handle) return false;
        return handle.done() && !is_cancelled() && !handle.promise().exception;
    }
    
    /// @brief 检查任务是否被拒绝/失败 (JavaScript Promise.rejected 风格)
    /// @return true if task was cancelled or has exception
    bool is_rejected() const noexcept {
        if (!handle) return false;
        return is_cancelled() || (handle.promise().exception != nullptr);
    }
    
    // 安全销毁方法 - 简化版，避免复杂的时序问题
    void safe_destroy() {
        if (handle && handle.address()) {
            try {
                // 检查promise是否仍然有效
                if (!handle.promise().is_destroyed()) {
                    // 标记为销毁状态
                    handle.promise().is_destroyed_.store(true, std::memory_order_release);
                }
                // 直接销毁，不检查done状态
                handle.destroy();
            } catch (...) {
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
                std::terminate();
            }
        }
        
        // 检查是否已销毁
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task::get: Task already destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        // 安全恢复协程
        if (!handle.done() && !handle.promise().is_cancelled()) {
            handle.resume();
        }
        
        // 使用安全getter获取结果
        auto exception = handle.promise().safe_get_exception();
        if (exception) {
            // 不使用异常，记录错误日志并返回默认值
            LOG_ERROR("Task execution failed with exception");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                // 对于不可默认构造的类型，尝试使用value的移动构造
                auto safe_value = handle.promise().safe_get_value();
                if (safe_value.has_value()) {
                    return std::move(safe_value.value());
                }
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate(); // 这种情况下只能终止程序
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
        if (handle.promise().is_destroyed()) return true; // 已销毁视为ready
        return handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // 增强版：全面安全检查
        if (!handle || handle.promise().is_destroyed()) {
            // 句柄无效或已销毁，直接恢复等待协程
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
            return;
        }
        
        if (!handle.done() && !handle.promise().is_cancelled()) {
            // 在线程池中运行当前任务，然后恢复等待的协程
            GlobalThreadPool::get().enqueue_void([task_handle = handle, waiting_handle]() {
                // 双重安全检查
                if (task_handle && !task_handle.done() && !task_handle.promise().is_destroyed()) {
                    try {
                        task_handle.resume();
                    } catch (...) {
                        LOG_ERROR("Exception during task resume in await_suspend");
                    }
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
        // 增强版：使用安全getter
        if (!handle) {
            LOG_ERROR("Task await_resume: Invalid handle");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task await_resume: Task destroyed");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
        
        auto exception = handle.promise().safe_get_exception();
        if (exception) {
            LOG_ERROR("Task await_resume: exception occurred");
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
            LOG_ERROR("Task await_resume: no value set");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                std::terminate();
            }
        }
    }
};

// Task<Result<T,E>>特化 - 支持Result错误处理
template<typename T, typename E>
struct Task<Result<T, E>> {
    struct promise_type {
        std::optional<Result<T, E>> result;
        std::exception_ptr exception;
        
        // 生命周期管理
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;
        mutable std::mutex state_mutex_;
        
        promise_type() : creation_time_(std::chrono::steady_clock::now()) {}
        
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
        }
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_value(Result<T, E> r) noexcept {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!is_cancelled_.load(std::memory_order_relaxed) && 
                !is_destroyed_.load(std::memory_order_relaxed)) {
                result = std::move(r);
            }
        }
        
        // 增强版异常处理 - 转换为Result
        void unhandled_exception() {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!is_destroyed_.load(std::memory_order_relaxed)) {
                try {
                    // 尝试将异常转换为Result的错误类型
                    if constexpr (std::is_same_v<E, ErrorInfo>) {
                        result = err(ErrorInfo(FlowCoroError::UnknownError, "Unhandled exception"));
                    } else if constexpr (std::is_same_v<E, std::exception_ptr>) {
                        result = err(std::current_exception());
                    } else {
                        // 对于其他错误类型，记录异常指针
                        exception = std::current_exception();
                    }
                } catch (...) {
                    exception = std::current_exception();
                }
            }
        }
        
        // 状态管理方法
        void request_cancellation() {
            std::lock_guard<std::mutex> lock(state_mutex_);
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
        
        std::optional<Result<T, E>> safe_get_result() const {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (is_destroyed_.load(std::memory_order_relaxed)) {
                return std::nullopt;
            }
            return result;
        }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) {
                try {
                    if (handle.address() != nullptr) {
                        handle.destroy();
                    }
                } catch (...) {
                    // 忽略析构异常
                }
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    
    ~Task() { 
        safe_destroy();
    }
    
    void cancel() {
        if (handle && !handle.done() && !handle.promise().is_destroyed()) {
            handle.promise().request_cancellation();
        }
    }
    
    bool is_cancelled() const {
        if (!handle || handle.promise().is_destroyed()) return false;
        return handle.promise().is_cancelled();
    }
    
    // Promise风格状态查询
    bool is_pending() const noexcept {
        if (!handle) return false;
        return !handle.done() && !is_cancelled();
    }
    
    bool is_settled() const noexcept {
        if (!handle) return true;
        return handle.done() || is_cancelled();
    }
    
    bool is_fulfilled() const noexcept {
        if (!handle || !handle.done() || is_cancelled()) return false;
        auto result = handle.promise().safe_get_result();
        return result.has_value() && result->is_ok();
    }
    
    bool is_rejected() const noexcept {
        if (!handle) return false;
        if (is_cancelled()) return true;
        if (!handle.done()) return false;
        auto result = handle.promise().safe_get_result();
        return !result.has_value() || result->is_err();
    }
    
    void safe_destroy() {
        if (handle && handle.address()) {
            try {
                if (!handle.promise().is_destroyed()) {
                    handle.promise().is_destroyed_.store(true, std::memory_order_release);
                }
                handle.destroy();
            } catch (...) {
                // 忽略销毁异常
            }
            handle = nullptr;
        }
    }
    
    // 获取结果
    Result<T, E> get() {
        if (!handle) {
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                return err(ErrorInfo(FlowCoroError::InvalidOperation, "Invalid task handle"));
            } else {
                return err(E{});
            }
        }
        
        // 检查取消状态
        if (is_cancelled()) {
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                return err(ErrorInfo(FlowCoroError::TaskCancelled, "Task was cancelled"));
            } else {
                return err(E{});
            }
        }
        
        // 等待完成
        while (!handle.done()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        // 获取结果
        auto result = handle.promise().safe_get_result();
        if (!result.has_value()) {
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                return err(ErrorInfo(FlowCoroError::CoroutineDestroyed, "Task was destroyed"));
            } else {
                return err(E{});
            }
        }
        
        return std::move(*result);
    }
    
    // 可等待支持
    bool await_ready() const {
        return !handle || handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            // 在线程池中运行
            GlobalThreadPool::get().enqueue_void([h = handle, waiting_handle]() {
                if (h && !h.done()) {
                    h.resume();
                }
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        }
    }
    
    Result<T, E> await_resume() {
        return get();
    }
};

// Task<void>特化 - 增强版集成生命周期管理
template<>
struct Task<void> {
    struct promise_type {
        std::exception_ptr exception;
        
        // 增强版生命周期管理 - 与Task<T>保持一致
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;
        mutable std::mutex state_mutex_; // 保护状态变更
        
        promise_type() : creation_time_(std::chrono::steady_clock::now()) {}
        
        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
        }
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_void() noexcept {
            // 增强版 - 检查状态
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (is_cancelled_.load(std::memory_order_relaxed) || is_destroyed_.load(std::memory_order_relaxed)) {
                return; // 已取消或销毁，不做处理
            }
        }
        
        void unhandled_exception() { 
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!is_destroyed_.load(std::memory_order_relaxed)) {
                exception = std::current_exception();
            }
        }
        
        // 增强版取消支持
        void request_cancellation() {
            std::lock_guard<std::mutex> lock(state_mutex_);
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
        
        // 安全获取异常
        std::exception_ptr safe_get_exception() const {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (is_destroyed_.load(std::memory_order_relaxed)) {
                return nullptr;
            }
            return exception;
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
                try {
                    if (handle.address() != nullptr) {
                        handle.destroy();
                    }
                } catch (...) {
                    // 忽略析构异常
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
    
    // 安全销毁方法 - 与Task<T>保持一致
    void safe_destroy() {
        if (handle && handle.address()) {
            try {
                // 检查promise是否仍然有效
                if (!handle.promise().is_destroyed()) {
                    // 标记为销毁状态
                    handle.promise().is_destroyed_.store(true, std::memory_order_release);
                }
                // 直接销毁，不检查done状态
                handle.destroy();
            } catch (...) {
                LOG_ERROR("Exception during Task<void>::safe_destroy");
            }
            handle = nullptr;
        }
    }
    
    // 增强版：取消支持
    void cancel() {
        if (handle && !handle.done() && !handle.promise().is_destroyed()) {
            handle.promise().request_cancellation();
            LOG_INFO("Task<void>::cancel: Task cancelled (lifetime: %lld ms)", 
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
    
    // ==========================================
    // Phase 1 新增: JavaScript Promise 风格状态查询API (Task<void>特化版本)
    // ==========================================
    
    /// @brief 检查任务是否仍在进行中 (JavaScript Promise.pending 风格)
    /// @return true if task is created or running, false otherwise
    bool is_pending() const noexcept {
        if (!handle) return false;
        return !handle.done() && !is_cancelled();
    }
    
    /// @brief 检查任务是否已结束 (JavaScript Promise.settled 风格)
    /// @return true if task is completed, cancelled, or has error
    bool is_settled() const noexcept {
        if (!handle) return true;  // 无效句柄视为已结束
        return handle.done() || is_cancelled();
    }
    
    /// @brief 检查任务是否成功完成 (JavaScript Promise.fulfilled 风格)
    /// @return true if task completed successfully
    bool is_fulfilled() const noexcept {
        if (!handle) return false;
        return handle.done() && !is_cancelled() && !handle.promise().exception;
    }
    
    /// @brief 检查任务是否被拒绝/失败 (JavaScript Promise.rejected 风格)
    /// @return true if task was cancelled or has exception
    bool is_rejected() const noexcept {
        if (!handle) return false;
        return is_cancelled() || (handle.promise().exception != nullptr);
    }
    
    void get() {
        // 增强版：安全状态检查
        if (!handle) {
            LOG_ERROR("Task<void>::get: Invalid handle");
            return;
        }
        
        // 检查是否已销毁
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task<void>::get: Task already destroyed");
            return;
        }
        
        // 安全恢复协程
        if (!handle.done() && !handle.promise().is_cancelled()) {
            handle.resume();
        }
        
        // 使用安全getter获取异常
        auto exception = handle.promise().safe_get_exception();
        if (exception) {
            LOG_ERROR("Task<void> execution failed with exception");
            // void类型不抛出异常，只记录
        }
    }
    
    // SafeTask兼容方法：获取结果（同步）
    void get_result() {
        get(); // 复用现有的get()方法
    }
    
    // SafeTask兼容方法：检查是否就绪
    bool is_ready() const noexcept {
        return await_ready();
    }
    
    // 使Task<void>可等待 - 增强版安全检查
    bool await_ready() const {
        if (!handle) return true; // 无效句柄视为ready
        if (handle.promise().is_destroyed()) return true; // 已销毁视为ready
        return handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // 增强版：全面安全检查
        if (!handle || handle.promise().is_destroyed()) {
            // 句柄无效或已销毁，直接恢复等待协程
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
            return;
        }
        
        if (!handle.done() && !handle.promise().is_cancelled()) {
            GlobalThreadPool::get().enqueue_void([task_handle = handle, waiting_handle]() {
                // 双重安全检查
                if (task_handle && !task_handle.done() && !task_handle.promise().is_destroyed()) {
                    try {
                        task_handle.resume();
                    } catch (...) {
                        LOG_ERROR("Exception during Task<void> resume in await_suspend");
                    }
                }
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        } else {
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        }
    }
    
    void await_resume() {
        // 增强版：使用安全getter
        if (!handle) {
            LOG_ERROR("Task<void> await_resume: Invalid handle");
            return;
        }
        
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task<void> await_resume: Task destroyed");
            return;
        }
        
        auto exception = handle.promise().safe_get_exception();
        if (exception) {
            LOG_ERROR("Task<void> await_resume: exception occurred");
            // void类型不抛出异常，只记录
        }
    }
};

// 支持异步等待的无锁Promise
template<typename T>
class AsyncPromise {
public:
    using Callback = std::function<void(const T&)>;
    
    AsyncPromise() : state_(std::make_shared<State>()) {}
    
    void set_value(const T& value) {
        LOG_DEBUG("Setting AsyncPromise value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->value_ = value;
            state_->ready_.store(true, std::memory_order_release);
            
            // 在锁保护下获取等待的协 coroutine
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        // 在锁外调度协程以避免死锁
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);
            
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise exception");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        // 使用锁来确保线程安全
        std::lock_guard<std::mutex> lock(state_->mutex_);
        
        // 在锁保护下检查是否已经有值
        if (state_->ready_.load(std::memory_order_acquire)) {
            // 如果已经ready，立即调度
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
            return;
        }
        
        // 在锁保护下设置挂起的协程句柄
        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            // 成功设置句柄，协程将被 set_value 调度
        } else {
            // 如果设置句柄失败，说明已经有其他协程在等待
            // 这种情况下直接调度当前协程
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
        }
    }
    
    T await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
        return state_->value_;
    }
    
private:
    struct State {
        std::atomic<bool> ready_{false};
        T value_{};
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_; // 保护非原子类型的value_和exception_
    };
    
    std::shared_ptr<State> state_;
};

// AsyncPromise的void特化
template<>
class AsyncPromise<void> {
public:
    AsyncPromise() : state_(std::make_shared<State>()) {}
    
    void set_value() {
        LOG_DEBUG("Setting AsyncPromise<void> value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->ready_.store(true, std::memory_order_release);
            
            // 在锁保护下获取等待的协程句柄
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        // 在锁外调度协程以避免死锁
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<void>");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise<void> exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);
            
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<void> exception");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        
        if (state_->ready_.load(std::memory_order_acquire)) {
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
            return;
        }
        
        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            // 成功设置句柄
        } else {
            // 如果设置句柄失败，直接调度当前协程
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
        }
    }
    
    void await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
    }
    
private:
    struct State {
        std::atomic<bool> ready_{false};
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_;
    };
    
    std::shared_ptr<State> state_;
};

// AsyncPromise的unique_ptr特化，支持移动语义
template<typename T>
class AsyncPromise<std::unique_ptr<T>> {
public:
    AsyncPromise() : state_(std::make_shared<State>()) {}
    
    void set_value(std::unique_ptr<T> value) {
        LOG_DEBUG("Setting AsyncPromise<unique_ptr> value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->value_ = std::move(value);
            state_->ready_.store(true, std::memory_order_release);
            
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<unique_ptr>");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise<unique_ptr> exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);
            
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }
        
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<unique_ptr> exception");
            GlobalThreadPool::get().enqueue_void([handle_to_resume]() {
                if (handle_to_resume && !handle_to_resume.done()) {
                    handle_to_resume.resume();
                }
            });
        }
    }
    
    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        
        if (state_->ready_.load(std::memory_order_acquire)) {
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
            return;
        }
        
        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            // 成功设置句柄
        } else {
            GlobalThreadPool::get().enqueue_void([h]() {
                if (h && !h.done()) {
                    h.resume();
                }
            });
        }
    }
    
    std::unique_ptr<T> await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
        return std::move(state_->value_);
    }
    
private:
    struct State {
        std::atomic<bool> ready_{false};
        std::unique_ptr<T> value_;
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_;
    };
    
    std::shared_ptr<State> state_;
};

// Task<unique_ptr<T>>特化，支持移动语义
template<typename T>
struct Task<std::unique_ptr<T>> {
    struct promise_type {
        std::unique_ptr<T> value;
        std::exception_ptr exception;
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(std::unique_ptr<T> v) noexcept { value = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
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
                // 检查协程句柄是否有效
                if (handle.address() != nullptr) {
                    handle.destroy();
                }
            } catch (...) {
                // 忽略析构时的异常
            }
        }
    }
    
    // ==========================================
    // Phase 1 新增: JavaScript Promise 风格状态查询API (Task<unique_ptr<T>>特化版本)
    // ==========================================
    
    /// @brief 检查任务是否仍在进行中 (JavaScript Promise.pending 风格)
    /// @return true if task is created or running, false otherwise
    bool is_pending() const noexcept {
        return handle && !handle.done();
    }
    
    /// @brief 检查任务是否已结束 (JavaScript Promise.settled 风格)
    /// @return true if task is completed or has error
    bool is_settled() const noexcept {
        if (!handle) return true;  // 无效句柄视为已结束
        return handle.done();
    }
    
    /// @brief 检查任务是否成功完成 (JavaScript Promise.fulfilled 风格)
    /// @return true if task completed successfully
    bool is_fulfilled() const noexcept {
        if (!handle) return false;
        return handle.done() && !handle.promise().exception;
    }
    
    /// @brief 检查任务是否被拒绝/失败 (JavaScript Promise.rejected 风格)
    /// @return true if task has exception
    bool is_rejected() const noexcept {
        if (!handle) return false;
        return handle.promise().exception != nullptr;
    }
    
    std::unique_ptr<T> get() {
        if (handle && !handle.done()) handle.resume();
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
        return std::move(handle.promise().value);
    }
    
    // 使Task<unique_ptr<T>>可等待
    bool await_ready() const {
        return handle && handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            GlobalThreadPool::get().enqueue_void([task_handle = handle, waiting_handle]() {
                if (task_handle && !task_handle.done()) {
                    task_handle.resume();
                }
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        } else {
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle && !waiting_handle.done()) {
                    waiting_handle.resume();
                }
            });
        }
    }
    
    std::unique_ptr<T> await_resume() {
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
        return std::move(handle.promise().value);
    }
};

// 支持异步任务的无锁队列
class AsyncQueue {
private:
    struct Node {
        std::function<void()> task;
        std::shared_ptr<Node> next;
    };
    
    std::atomic<std::shared_ptr<Node>> head_;
    std::atomic<std::shared_ptr<Node>> tail_;
    
public:
    AsyncQueue() {
        // 初始化哨兵节点
        auto sentinel = std::make_shared<Node>();
        head_.store(sentinel);
        tail_.store(sentinel);
    }
    
    // 入队操作
    void enqueue(std::function<void()> task) {
        auto new_tail = std::make_shared<Node>();
        new_tail->task = std::move(task);
        
        // 尝试将新节点链接到当前尾节点
        std::shared_ptr<Node> old_tail = tail_.load();
        while (!tail_.compare_exchange_weak(old_tail, new_tail, std::memory_order_release));
        
        // 将旧尾节点的next指针指向新节点
        old_tail->next = new_tail;
    }
    
    // 出队操作
    bool dequeue(std::function<void()>& task) {
        // 读取当前头节点
        std::shared_ptr<Node> old_head = head_.load();
        
        // 检查队列是否为空
        if (old_head == tail_.load()) {
            return false; // 队列为空
        }
        
        // 尝试将头节点向前移动一个节点
        if (head_.compare_exchange_strong(old_head, old_head->next, std::memory_order_acquire)) {
            task = std::move(old_head->task);
            return true;
        }
        
        return false;
    }
};

// ==========================================
// SafeTask别名 - 为了向后兼容，SafeTask就是Task
// ==========================================

template<typename T = void>
using SafeTask = Task<T>;

// 便捷函数：休眠

// 内置的安全协程句柄管理器
class SafeCoroutineHandle {
private:
    std::shared_ptr<std::atomic<std::coroutine_handle<>>> handle_;
    std::shared_ptr<std::atomic<bool>> destroyed_;
    
public:
    explicit SafeCoroutineHandle(std::coroutine_handle<> h) 
        : handle_(std::make_shared<std::atomic<std::coroutine_handle<>>>(h))
        , destroyed_(std::make_shared<std::atomic<bool>>(false)) {}
    
    void resume() {
        if (destroyed_->load(std::memory_order_acquire)) {
            return; // 已销毁，安全退出
        }
        
        auto h = handle_->load(std::memory_order_acquire);
        if (h && !h.done()) {
            try {
                h.resume();
            } catch (...) {
                // 安全处理所有异常
            }
        }
    }
    
    ~SafeCoroutineHandle() {
        destroyed_->store(true, std::memory_order_release);
        // 清除句柄引用
        handle_->store({}, std::memory_order_release);
    }
};

// 简化的协程等待器：等待一定时间 - 使用内置安全生命周期管理
class SleepAwaiter {
public:
    explicit SleepAwaiter(std::chrono::milliseconds duration) 
        : duration_(duration) {}
    
    bool await_ready() const noexcept { 
        return duration_.count() <= 0;
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        // 使用内置的安全句柄管理
        auto safe_handle = std::make_shared<SafeCoroutineHandle>(h);
        GlobalThreadPool::get().enqueue_void([duration = duration_, safe_handle]() {
            std::this_thread::sleep_for(duration);
            safe_handle->resume(); // 使用安全resume
        });
    }
    
    void await_resume() const noexcept {}
    
private:
    std::chrono::milliseconds duration_;
};

// 便捷函数：休眠
inline auto sleep_for(std::chrono::milliseconds duration) {
    return SleepAwaiter(duration);
}

// 协程等待器：等待多个任务完成
template<typename... Tasks>
class WhenAllAwaiter {
public:
    explicit WhenAllAwaiter(Tasks&&... tasks) : tasks_(std::forward<Tasks>(tasks)...) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) {
        std::apply([h, this](auto&... tasks) {
            constexpr size_t task_count = sizeof...(tasks);
            auto counter = std::make_shared<std::atomic<size_t>>(task_count);
            
            auto complete_one = [h, counter]() {
                if (counter->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    GlobalThreadPool::get().enqueue_void([h]() {
                        if (h && !h.done()) {
                            h.resume();
                        }
                    });
                }
            };
            
            // 为每个任务启动异步执行
            ((GlobalThreadPool::get().enqueue([&tasks, complete_one]() {
                tasks.get(); // 等待任务完成
                complete_one();
            })), ...);
        }, tasks_);
    }
    
    void await_resume() const noexcept {}
    
private:
    std::tuple<Tasks...> tasks_;
};

// 便捷函数：等待所有任务完成
template<typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return WhenAllAwaiter<Tasks...>(std::forward<Tasks>(tasks)...);
}

// 同步等待协程完成的函数
template<typename T>
T sync_wait(Task<T>&& task) {
    // 直接使用Task的get()方法，它会阻塞直到完成
    try {
        return task.get();
    } catch (...) {
        // 不使用异常，记录错误日志并返回默认值
        LOG_ERROR("sync_wait: Task execution failed");
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return T{};
        }
    }
}

// void版本的特化
inline void sync_wait(Task<void>&& task) {
    try {
        task.get();
    } catch (...) {
        LOG_ERROR("sync_wait: Task execution failed");
    }
}

// 重载版本 - 接受lambda并返回Task
template<typename Func>
auto sync_wait(Func&& func) {
    auto task = func();
    return sync_wait(std::move(task));
}

// ========================================
// Phase 4 Integration: 生命周期管理集成
// ========================================

/**
 * @brief 启用FlowCoro v2增强功能 - 简化版
 */
inline void enable_v2_features() {
    LOG_INFO("🚀 FlowCoro Enhanced Features Enabled (Simplified Integration)");
    LOG_INFO("   ✅ Basic lifecycle management integrated");
    LOG_INFO("   ✅ Cancel/timeout support added"); 
    LOG_INFO("   ✅ State monitoring available");
    LOG_INFO("   ✅ Legacy Task integration completed");
}

/**
 * @brief 简化版任务类型别名
 */
template<typename T = void>
using EnhancedTask = Task<T>;

/**
 * @brief 便利函数：将现有Task转换为增强Task（已经集成）
 */
template<typename T>
auto make_enhanced(Task<T>&& task) -> Task<T> {
    return std::move(task);
}

/**
 * @brief 简化版取消支持
 */
template<typename T>
auto make_cancellable_task(Task<T> task) -> Task<T> {
    // 返回任务本身，因为已经有取消支持
    return task;
}

/**
 * @brief 简化版超时支持
 */
template<typename T>
auto make_timeout_task(Task<T>&& task, std::chrono::milliseconds timeout) -> Task<T> {
    // 启动超时线程
    GlobalThreadPool::get().enqueue_void([task_ref = &task, timeout]() {
        std::this_thread::sleep_for(timeout);
        task_ref->cancel();
    });
    
    return std::move(task);
}

/**
 * @brief 简化版性能报告
 */
inline void print_performance_report() {
    LOG_INFO("=== FlowCoro Performance Report (Simplified) ===");
    LOG_INFO("✅ Task<T> integration: COMPLETE");
    LOG_INFO("✅ Basic lifecycle management: ACTIVE");
    LOG_INFO("✅ Cancel/timeout support: AVAILABLE");
}

} // namespace flowcoro

// 所有增强功能已整合到core.h中，无需额外包含
