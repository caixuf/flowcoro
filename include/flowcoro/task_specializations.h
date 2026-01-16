#pragma once
// Task - 

namespace flowcoro {

// Task<Result<T,E>> - Result
template<typename T, typename E>
struct Task<Result<T, E>> {
    struct promise_type {
        std::optional<Result<T, E>> result;
        std::exception_ptr exception;

        // 
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
            // 
            if (!is_cancelled_.load(std::memory_order_relaxed)) [[likely]] {
                result = std::move(r);
            }
        }

        //  - Result
        void unhandled_exception() {
            // 
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                result = err(ErrorInfo(FlowCoroError::UnknownError, "Unhandled exception"));
            } else if constexpr (std::is_same_v<E, std::exception_ptr>) {
                result = err(std::exception_ptr{}); // 
            } else {
                // 
                result = err(E{});
            }
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

        std::optional<Result<T, E>> safe_get_result() const noexcept {
            // 
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

    // Promise
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

    // 
    Result<T, E> get() {
        if (!handle) {
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                return err(ErrorInfo(FlowCoroError::InvalidOperation, "Invalid task handle"));
            } else {
                return err(E{});
            }
        }

        // 
        if (is_cancelled()) {
            if constexpr (std::is_same_v<E, ErrorInfo>) {
                return err(ErrorInfo(FlowCoroError::TaskCancelled, "Task was cancelled"));
            } else {
                return err(E{});
            }
        }

        //  - 
        while (!handle.done()) {
            // 
            for (int i = 0; i < 100 && !handle.done(); ++i) {
                std::this_thread::yield();
            }
            // 
            if (!handle.done()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }

        // 
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

    // 
    bool await_ready() const {
        return !handle || handle.done();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
            manager.schedule_resume(waiting_handle);
        }
    }

    Result<T, E> await_resume() {
        return get();
    }
};

// Task<void> - 
template<>
struct Task<void> {
    struct promise_type {
        bool has_error = false; // exception_ptr
        std::coroutine_handle<> continuation; // Taskcontinuation

        //  - Task<T>
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
            } else {
                PerformanceMonitor::get_instance().on_task_completed();
            }
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }  //  - Task<T>
        
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

        void return_void() noexcept {
            // return_void
        }

        void unhandled_exception() {
            // 
            has_error = true;
            LOG_ERROR("Task<void> unhandled exception occurred");
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
        // 
        safe_destroy();
    }

    //  - Task<T>
    void safe_destroy() {
        if (handle && handle.address()) {
            // promise
            if (!handle.promise().is_destroyed()) {
                // 
                handle.promise().is_destroyed_.store(true, std::memory_order_release);
            }
            // done
            if (handle.address() != nullptr) {
                handle.destroy();
            } else {
                LOG_ERROR("Task<void>::safe_destroy: handle address is null");
            }
            handle = nullptr;
        }
    }

    // 
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

    // 
    std::chrono::milliseconds get_lifetime() const {
        if (!handle) return std::chrono::milliseconds{0};
        return handle.promise().get_lifetime();
    }

    bool is_active() const {
        return handle && !handle.done() && !is_cancelled();
    }

    // JavaScript Promise API (Task<void>)
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

    void get() {
        // 
        if (!handle) {
            LOG_ERROR("Task<void>::get: Invalid handle");
            return;
        }

        // 
        if (handle.promise().is_destroyed()) {
            LOG_ERROR("Task<void>::get: Task already destroyed");
            return;
        }

        //  suspend_never 
        if (handle.done()) {
            // 
            if (handle.promise().safe_has_error()) {
                LOG_ERROR("Task<void> execution failed");
            }
            return;
        }

        // 
        if (!handle.promise().is_cancelled()) {
            auto& manager = CoroutineManager::get_instance();
            
            try {
                manager.schedule_resume(handle);
            } catch (const std::exception& e) {
                LOG_ERROR("Task<void>::get: Exception during schedule_resume: %s", e.what());
                return;
            } catch (...) {
                LOG_ERROR("Task<void>::get: Unknown exception during schedule_resume");
                return;
            }
            
            //  
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5);
            
            auto wait_time = std::chrono::microseconds(1);
            const auto max_wait = std::chrono::microseconds(100);
            size_t spin_count = 0;
            const size_t max_spins = 100;
            
            while (!handle.done() && !handle.promise().is_cancelled()) {
                // 
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

        // 
        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<void> execution failed");
        }
    }

    // SafeTask
    void get_result() {
        get(); // get()
    }

    // SafeTask
    bool is_ready() const noexcept {
        return await_ready();
    }

    // Task<void> - 
    bool await_ready() const {
        if (!handle) return true; // ready
        if (handle.promise().is_destroyed()) return true; // ready
        return handle.done();
    }

    bool await_suspend(std::coroutine_handle<> waiting_handle) {
        // Task<void> - Task<T>
        if (!handle || handle.promise().is_destroyed()) {
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return false;
        }

        if (handle.done()) {
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
            return false;
        }

        // continuationtaskwaiting_handle
        handle.promise().set_continuation(waiting_handle);

        // taskcontinuation
        return true;
    }

    void await_resume() {
        // getter
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

// Task<unique_ptr<T>>
template<typename T>
struct Task<std::unique_ptr<T>> {
    struct promise_type {
        std::unique_ptr<T> value;
        bool has_error = false;
        std::coroutine_handle<> continuation;

        // 
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

    // Promise
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
                // 
                for (int i = 0; i < 100 && !handle.done() && !handle.promise().is_cancelled(); ++i) {
                    manager.drive();
                    std::this_thread::yield();
                }
                // 
                if (!handle.done() && !handle.promise().is_cancelled()) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        }

        if (handle.promise().safe_has_error()) {
            LOG_ERROR("Task<unique_ptr> execution failed");
            return nullptr;
        }

        return handle.promise().safe_get_value();
    }

    // 
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
