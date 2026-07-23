#pragma once
// Task特化版本 - 分离以减少编译依赖

namespace flowcoro {

// Task<Result<T,E>>特化 - 支持Result错误处理
template<typename T, typename E>
struct Task<Result<T, E>> {
    struct promise_type {
        std::optional<Result<T, E>> result;
        std::exception_ptr exception; // 保存异常

        // 生命周期管理
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::atomic<bool> completed_{false};
#ifndef NDEBUG
        std::chrono::steady_clock::time_point creation_time_;
#endif

        promise_type()
#ifndef NDEBUG
            : creation_time_(std::chrono::steady_clock::now())
#endif
        {}

        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        // 用自定义 final_suspend 设置 completed_ 标志
        auto final_suspend() noexcept {
            struct final_awaiter {
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept {
                    return std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            // 在 final_suspend 入口设置 completed_（release），让 get()
            // 的 load(acquire) 能观察到完成状态。
            completed_.store(true, std::memory_order_release);
            return final_awaiter{};
        }

        void return_value(Result<T, E> r) noexcept {
            // 快速路径：通常情况下协程没有被取消
            if (!is_cancelled_.load(std::memory_order_relaxed)) [[likely]] {
                result = std::move(r);
            }
        }

        // 快速异常处理 - 转换为Result，去除锁
        void unhandled_exception() {
            // 保存异常
            exception = std::current_exception();
            // 不使用异常，只设置错误状态
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                result = err(ErrorInfo(FlowCoroError::UnknownError, "Unhandled exception"));
            } else if constexpr (std::is_same_v<E, std::exception_ptr>) {
                result = err(std::current_exception());
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

        bool is_completed() const noexcept {
            return completed_.load(std::memory_order_acquire);
        }

        std::chrono::milliseconds get_lifetime() const {
#ifndef NDEBUG
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_);
#else
            return std::chrono::milliseconds{0};
#endif
        }

        std::optional<Result<T, E>> safe_get_result() const noexcept {
            // 快速路径：通常情况下协程没有被销毁
            if (!is_destroyed_.load(std::memory_order_acquire)) [[likely]] {
                return result;
            }
            return std::nullopt;
        }

        // 获取异常
        std::exception_ptr get_exception() const noexcept {
            return exception;
        }

        // 重新抛出异常
        void rethrow_if_exception() const {
            if (exception) {
                std::rethrow_exception(exception);
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
        return handle.promise().completed_.load(std::memory_order_acquire) || is_cancelled();
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
            // 统一用延迟销毁（process_pending_tasks 无条件销毁，RAII 契约）。
            CoroutineManager::get_instance().schedule_destroy(handle);
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

        // 等待完成 - 快速自适应等待
        while (!handle.done()) {
            // 快速检查几次
            for (int i = 0; i < 100 && !handle.done(); ++i) {
                std::this_thread::yield();
            }
            // 如果还没完成，短暂休眠
            if (!handle.done()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
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

    // 不抛异常版本（向后兼容） - 对于Result类型，已经不抛异常
    std::optional<Result<T, E>> try_get() noexcept {
        if (!handle || handle.promise().is_destroyed()) {
            return std::nullopt;
        }
        return handle.promise().safe_get_result();
    }

    // 获取错误信息
    std::optional<std::string> get_error_message() const noexcept {
        if (!handle || !handle.promise().exception) {
            return std::nullopt;
        }
        try {
            std::rethrow_exception(handle.promise().exception);
        } catch (const std::exception& e) {
            return std::string(e.what());
        } catch (...) {
            return std::string("Unknown exception");
        }
    }
};

// Task<void>特化 - 增强版集成生命周期管理
template<>
struct Task<void> {
    struct promise_type {
        bool has_error = false; // 替换exception_ptr
        std::exception_ptr exception_; // 保存异常
        std::coroutine_handle<> continuation; // 懒加载Task的continuation支持

        // 增强版生命周期管理 - 与Task<T>保持一致
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        // 协程是否已到达 final_suspend（已完成）。
        // 在 final_suspend::await_ready 里 store(true, release)，
        // 让 get() 可以用 load(acquire) 代替 handle.done() 跨线程检测完成，
        // 规避 GCC coroutine_handle::done() 非线程安全的问题。
        std::atomic<bool> completed_{false};
#ifndef NDEBUG
        std::chrono::steady_clock::time_point creation_time_;
#endif

        promise_type()
#ifndef NDEBUG
            : creation_time_(std::chrono::steady_clock::now())
#endif
        {
#ifndef NDEBUG
            PerformanceMonitor::get_instance().on_task_created();
#endif
        }

        // 析构时标记销毁
        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
#ifndef NDEBUG
            if (has_error) {
                PerformanceMonitor::get_instance().on_task_failed();
            } else if (is_cancelled()) {
                PerformanceMonitor::get_instance().on_task_cancelled();
            } else {
                PerformanceMonitor::get_instance().on_task_completed();
            }
#endif
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }  // 同步执行直到首个挂起点 - 与Task<T>保持一致
        
        // 支持continuation的final_suspend
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise;

                bool await_ready() const noexcept {
                    // 标记协程已完成（release），让 get() 的 load(acquire)
                    // 能观察到完成状态，无需调用 handle.done()。
                    promise->completed_.store(true, std::memory_order_release);
                    return false;
                }

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
            exception_ = std::current_exception(); // 捕获异常
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

        bool is_completed() const noexcept {
            return completed_.load(std::memory_order_acquire);
        }

        std::chrono::milliseconds get_lifetime() const {
#ifndef NDEBUG
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_);
#else
            return std::chrono::milliseconds{0};
#endif
        }

        // 快速获取错误状态 - 去除锁
        bool safe_has_error() const noexcept {
            return has_error && !is_destroyed_.load(std::memory_order_acquire);
        }

        // 获取异常
        std::exception_ptr get_exception() const noexcept {
            return exception_;
        }

        // 重新抛出异常
        void rethrow_if_exception() const {
            if (exception_) {
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

    // 放弃对协程帧的所有权：置空 handle，析构时不调用 safe_destroy。
    // 语义同 Task<T>::detach()，用于 when_any 等跨线程场景。
    void detach() noexcept {
        handle = nullptr;
    }

    // 安全销毁方法 - 与Task<T>保持一致
    void safe_destroy() {
        if (handle && handle.address()) {
            // 检查promise是否仍然有效
            if (!handle.promise().is_destroyed()) {
                // 标记为销毁状态
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }
            // 统一用延迟销毁：不在此处调用 handle.destroy() 或 handle.done()。
            // 原因：GCC 的 coroutine_handle::destroy()/done() 会读取协程帧
            // 内部的 __resume_index 字段，跨线程访问是 data race（TSAN 报警）。
            // schedule_destroy 把帧入队，由 CoroutineManager 无条件销毁（RAII 契约）。
            CoroutineManager::get_instance().schedule_destroy(handle);
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
        return handle.promise().completed_.load(std::memory_order_acquire) || is_cancelled();
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

        // 关键修复：先检查是否已完成（suspend_never 情况下协程会同步执行直到挂起）
        // 用 is_completed() 代替 handle.done()：前者用原子 load(acquire)，
        // 后者读取协程帧内部 __resume_index，跨线程不安全（TSAN 报警）。
        if (handle.promise().is_completed()) {
            // 重新抛出异常（如果有）
            handle.promise().rethrow_if_exception();
            return;
        }

        // 只有在未完成时才进入调度逻辑
        if (!handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            
            // 🔧 修复：不直接调度外层句柄，通过驱动管理器让子任务完成并触发延续链
            // 🔧 Fix: do not schedule the outer handle directly; drive the manager so sub-tasks
            //         complete via their natural mechanisms (timers/continuations) and
            //         eventually resume this handle.
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5);
            
            auto wait_time = std::chrono::microseconds(1);
            const auto max_wait = std::chrono::microseconds(100);
            size_t spin_count = 0;
            const size_t max_spins = 100;
            
            while (!handle.promise().is_completed() && !handle.promise().is_cancelled()) {
                // 超时检查
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > timeout) {
                    LOG_ERROR("Task<void>::get: Timeout after 5 seconds");
                    return;
                }
                
                manager.drive();
                
                if (spin_count < max_spins) {
                    ++spin_count;
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(wait_time);
                    wait_time = std::min(wait_time * 2, max_wait);
                }
            }
        }

        // 检查是否有错误
        if (handle.promise().safe_has_error()) {
            // 重新抛出异常
            handle.promise().rethrow_if_exception();
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
        // Task<void>版本 - 与Task<T>保持一致的实现
        if (!handle || handle.promise().is_destroyed()) {
            // 句柄无效：返回 false，C++ 机制立即恢复等待协程（无需 schedule_resume）
            // 注意：return false 时协程立即被恢复，再调用 schedule_resume 会导致双重唤醒崩溃
            return false;
        }

        if (handle.done()) {
            // 任务已完成：同上，return false 会立即恢复等待协程
            return false;
        }

        // 设置continuation：当task完成时通过 final_suspend 恢复 waiting_handle
        handle.promise().set_continuation(waiting_handle);

        // 挂起等待协程，等待task通过continuation唤醒
        return true;
    }

    void await_resume() {
        // 增强版：使用安全getter
        if (!handle) {
            throw std::runtime_error("Task<void> await_resume: Invalid handle");
        }

        if (handle.promise().is_destroyed()) {
            throw std::runtime_error("Task<void> await_resume: Task destroyed");
        }

        // 重新抛出异常
        handle.promise().rethrow_if_exception();
    }

    // 不抛异常版本（向后兼容）
    bool try_get() noexcept {
        if (!handle || handle.promise().is_destroyed()) {
            return false;
        }
        if (handle.promise().has_error) {
            return false;
        }
        return true;
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

// Task<unique_ptr<T>>特化，支持移动语义
template<typename T>
struct Task<std::unique_ptr<T>> {
    struct promise_type {
        std::unique_ptr<T> value;
        bool has_error = false;
        std::exception_ptr exception_; // 保存异常
        std::coroutine_handle<> continuation;

        // 生命周期管理
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::atomic<bool> completed_{false};
#ifndef NDEBUG
        std::chrono::steady_clock::time_point creation_time_;
#endif

        promise_type()
#ifndef NDEBUG
            : creation_time_(std::chrono::steady_clock::now())
#endif
        {
#ifndef NDEBUG
            PerformanceMonitor::get_instance().on_task_created();
#endif
        }

        ~promise_type() {
            is_destroyed_.store(true, std::memory_order_release);
#ifndef NDEBUG
            if (has_error) {
                PerformanceMonitor::get_instance().on_task_failed();
            } else if (is_cancelled()) {
                PerformanceMonitor::get_instance().on_task_cancelled();
            } else if (value != nullptr) {
                PerformanceMonitor::get_instance().on_task_completed();
            }
#endif
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise;

                bool await_ready() const noexcept {
                    promise->completed_.store(true, std::memory_order_release);
                    return false;
                }

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
            exception_ = std::current_exception(); // 捕获异常
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

        bool is_completed() const noexcept {
            return completed_.load(std::memory_order_acquire);
        }

        std::chrono::milliseconds get_lifetime() const {
#ifndef NDEBUG
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_);
#else
            return std::chrono::milliseconds{0};
#endif
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

        // 获取异常
        std::exception_ptr get_exception() const noexcept {
            return exception_;
        }

        // 重新抛出异常
        void rethrow_if_exception() const {
            if (exception_) {
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

    // 放弃对协程帧的所有权：置空 handle，析构时不调用 safe_destroy。
    // 语义同 Task<T>::detach()，用于 when_any 等跨线程场景。
    void detach() noexcept {
        handle = nullptr;
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
        return handle.promise().completed_.load(std::memory_order_acquire) || is_cancelled();
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

            // 统一用延迟销毁，不调用 handle.done()/destroy()（跨线程不安全）
            // process_pending_tasks 无条件销毁入队帧（RAII 契约）。
            manager.schedule_destroy(handle);
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
            
            while (!handle.promise().is_completed() && !handle.promise().is_cancelled()) {
                manager.drive();
                // 快速检查几次
                for (int i = 0; i < 100 && !handle.done() && !handle.promise().is_cancelled(); ++i) {
                    manager.drive();
                    std::this_thread::yield();
                }
                // 如果还没完成，短暂休眠
                if (!handle.done() && !handle.promise().is_cancelled()) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        }

        if (handle.promise().safe_has_error()) {
            // 重新抛出异常
            handle.promise().rethrow_if_exception();
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
            throw std::runtime_error("Task<unique_ptr> await_resume: Invalid handle");
        }

        if (handle.promise().is_destroyed()) {
            throw std::runtime_error("Task<unique_ptr> await_resume: Task destroyed");
        }

        // 重新抛出异常
        handle.promise().rethrow_if_exception();

        return handle.promise().safe_get_value();
    }

    // 不抛异常版本（向后兼容）
    std::unique_ptr<T> try_get() noexcept {
        if (!handle || handle.promise().is_destroyed()) {
            return nullptr;
        }
        if (handle.promise().has_error) {
            return nullptr;
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
