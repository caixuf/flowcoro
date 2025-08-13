#pragma once
// Task特化版本 - 分离以减少编译依赖

namespace flowcoro {

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
            // 快速路径：通常情况下协程没有被取消
            if (!is_cancelled_.load(std::memory_order_relaxed)) [[likely]] {
                result = std::move(r);
            }
        }

        // 快速异常处理 - 转换为Result，去除锁
        void unhandled_exception() {
            // 不使用异常，只设置错误状态
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                result = err(ErrorInfo(FlowCoroError::UnknownError, "Unhandled exception"));
            } else if constexpr (std::is_same_v<E, std::exception_ptr>) {
                result = err(std::exception_ptr{}); // 不能捕获异常，只返回空指针
            } else {
                // 对于其他错误类型，只能设置为默认错误
                result = err(E{});
            }
        }

        // 快速状态管理方法 - 去除锁
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

        std::optional<Result<T, E>> safe_get_result() const noexcept {
            // 快速路径：通常情况下协程没有被销毁
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                return result;
            }
            return std::nullopt;
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
                if (handle.address() != nullptr) {
                    handle.destroy();
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
            if (!handle.promise().is_destroyed()) {
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }
            handle.destroy();
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
            // 使用协程管理器统一调度
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            manager.schedule_resume(waiting_handle);
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
        bool has_error = false; // 替换exception_ptr
        std::coroutine_handle<> continuation; // 懒加载Task的continuation支持

        // 增强版生命周期管理 - 与Task<T>保持一致
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;

        promise_type() : creation_time_(std::chrono::steady_clock::now()) {}

        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }  // 懒执行
        
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

        void return_void() noexcept {
            // return_void本身就是空操作，不需要额外检查
        }

        void unhandled_exception() {
            // 快速路径：直接设置错误标志
            has_error = true;
            LOG_ERROR("Task<void> unhandled exception occurred");
        }

        // Continuation支持
        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation = cont;
        }

        // 快速取消支持 - 去除锁
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
                if (handle.address() != nullptr) {
                    handle.destroy();
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
            // 检查promise是否仍然有效
            if (!handle.promise().is_destroyed()) {
                // 标记为销毁状态
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }
            // 直接销毁，不检查done状态
            if (handle.address() != nullptr) {
                handle.destroy();
            } else {
                LOG_ERROR("Task<void>::safe_destroy: handle address is null");
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

    // JavaScript Promise 风格状态查询API (Task<void>特化版本)
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

        // 支持懒执行：启动协程并驱动事件循环
        if (!handle.done() && !handle.promise().is_cancelled()) {
            // 获取协程管理器实例
            auto& manager = CoroutineManager::get_instance();
            
            // 启动协程（如果尚未开始）- 使用协程管理器调度
            try {
                manager.schedule_resume(handle);
            } catch (const std::exception& e) {
                LOG_ERROR("Task<void>::get: Exception during schedule_resume: %s", e.what());
                return;
            } catch (...) {
                LOG_ERROR("Task<void>::get: Unknown exception during schedule_resume");
                return;
            }
            
            // 等待协程完成（同步等待）
            while (!handle.done() && !handle.promise().is_cancelled()) {
                manager.drive(); // 驱动协程池执行
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // 检查是否有错误
        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<void> execution failed");
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

    bool await_suspend(std::coroutine_handle<> waiting_handle) {
        // 懒加载Task的正确实现 - Task<void>版本
        if (!handle || handle.promise().is_destroyed()) {
            // 句柄无效，直接恢复等待协程
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return false;
        }

        if (handle.done()) {
            // 任务已完成，直接恢复等待协程
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return false;
        }

        // 设置continuation：当task完成时恢复waiting_handle
        handle.promise().set_continuation(waiting_handle);

        // 启动懒加载的任务执行（仅首次）
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(handle);

        // 挂起等待协程，等待task通过continuation唤醒
        return true;
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

        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<void> await_resume: error occurred");
        }
    }
};

// Task<unique_ptr<T>>特化，支持移动语义
template<typename T>
struct Task<std::unique_ptr<T>> {
    struct promise_type {
        std::unique_ptr<T> value;
        bool has_error = false;
        std::coroutine_handle<> continuation;

        // 生命周期管理
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;

        promise_type() : creation_time_(std::chrono::steady_clock::now()) {
            PerformanceMonitor::get_instance().on_task_created();
        }

        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
            
            if (has_error) {
                PerformanceMonitor::get_instance().on_task_failed();
            } else if (is_cancelled()) {
                PerformanceMonitor::get_instance().on_task_cancelled();
            } else if (value != nullptr) {
                PerformanceMonitor::get_instance().on_task_completed();
            }
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise;
                
                bool await_ready() const noexcept { return false; }
                
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    if (promise->continuation) {
                        return promise->continuation;
                    }
                    return std::noop_coroutine();
                }
                
                void await_resume() const noexcept {}
            };
            return final_awaiter{this};
        }

        void return_value(std::unique_ptr<T> v) noexcept {
            if (!is_cancelled_.load(std::memory_order_relaxed)) [[likely]] {
                value = std::move(v);
            }
        }

        void unhandled_exception() {
            has_error = true;
            LOG_ERROR("Task<unique_ptr> unhandled exception occurred");
        }

        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation = cont;
        }

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

        std::unique_ptr<T> safe_get_value() noexcept {
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                return std::move(value);
            }
            return nullptr;
        }

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
            if (handle) {
                if(handle.address() != nullptr && !handle.done()) {
                    handle.promise().request_cancellation();
                    safe_destroy();
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
        if (!handle) return false;
        return handle.done() && !is_cancelled() && !handle.promise().has_error;
    }

    bool is_rejected() const noexcept {
        if (!handle) return false;
        return is_cancelled() || handle.promise().has_error;
    }

    void safe_destroy() {
        if (handle && handle.address()) {
            auto& manager = CoroutineManager::get_instance();

            if (!handle.promise().is_destroyed()) {
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }

            if (handle.done()) {
                handle.destroy();
            } else {
                manager.schedule_destroy(handle);
            }
            handle = nullptr;
        }
    }

    std::unique_ptr<T> get() {
        if (!handle) {
            LOG_ERROR("Task<unique_ptr>::get: Invalid handle");
            return nullptr;
        }

        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task<unique_ptr>::get: Task already destroyed");
            return nullptr;
        }

        if (!handle.done() && !handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            
            while (!handle.done() && !handle.promise().is_cancelled()) {
                manager.drive();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<unique_ptr> execution failed");
            return nullptr;
        }

        return handle.promise().safe_get_value();
    }

    // 可等待支持
    bool await_ready() const {
        if (!handle) return true;
        if (!handle.address()) return true;
        if (handle.done()) return true;
        return handle.promise().is_destroyed();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (!handle || handle.promise().is_destroyed()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        if (handle.done()) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return;
        }

        handle.promise().set_continuation(waiting_handle);
    }

    std::unique_ptr<T> await_resume() {
        if (!handle) {
            LOG_ERROR("Task<unique_ptr> await_resume: Invalid handle");
            return nullptr;
        }

        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task<unique_ptr> await_resume: Task destroyed");
            return nullptr;
        }

        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<unique_ptr> await_resume: error occurred");
            return nullptr;
        }

        return handle.promise().safe_get_value();
    }
};

} // namespace flowcoro
