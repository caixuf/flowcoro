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
#include "lockfree.h"
#include "thread_pool.h"
#include "logger.h"
#include "buffer.h"

// 前向声明HttpRequest类
class HttpRequest;

namespace flowcoro {

// 简化的全局线程池
class GlobalThreadPool {
private:
    static std::atomic<bool> shutdown_requested_;
    static std::atomic<lockfree::ThreadPool*> instance_;
    static std::once_flag init_flag_;
    
    static void init() {
        static lockfree::ThreadPool* pool = new lockfree::ThreadPool(std::thread::hardware_concurrency());
        instance_.store(pool, std::memory_order_release);
    }
    
public:
    static lockfree::ThreadPool& get() {
        std::call_once(init_flag_, init);
        return *instance_.load(std::memory_order_acquire);
    }
    
    static bool is_shutdown_requested() {
        return shutdown_requested_.load(std::memory_order_acquire);
    }
    
    static void shutdown() {
        shutdown_requested_.store(true, std::memory_order_release);
        // 获取线程池实例并调用 shutdown，但不要删除它
        lockfree::ThreadPool* pool = instance_.load(std::memory_order_acquire);
        if (pool) {
            pool->shutdown();
        }
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

// 支持返回值的Task

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
        void return_value(T v) noexcept { value = std::move(v); }
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
    ~Task() { if (handle) handle.destroy(); }
    T get() {
        if (handle && !handle.done()) handle.resume();
        if (handle.promise().exception) {
            // 不使用异常，记录错误日志并返回默认值
            LOG_ERROR("Task execution failed with exception");
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            } else {
                // 对于不可默认构造的类型，尝试使用value的移动构造
                if (handle.promise().value.has_value()) {
                    return std::move(handle.promise().value.value());
                }
                LOG_ERROR("Cannot provide default value for non-default-constructible type");
                std::terminate(); // 这种情况下只能终止程序
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
    
    // 使Task可等待
    bool await_ready() const {
        return handle && handle.done();
    }
    
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) {
            // 在线程池中运行当前任务，然后恢复等待的协程
            GlobalThreadPool::get().enqueue_void([task_handle = handle, waiting_handle]() {
                if (task_handle && !task_handle.done()) {
                    task_handle.resume();
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
    ~Task() { if (handle) handle.destroy(); }
    void get() {
        if (handle && !handle.done()) handle.resume();
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
    }
    
    // 使Task<void>可等待
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
    
    void await_resume() {
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
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
    ~Task() { if (handle) handle.destroy(); }
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

// 协程等待器：等待一定时间
class SleepAwaiter {
public:
    explicit SleepAwaiter(std::chrono::milliseconds duration) 
        : duration_(duration) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) {
        // 检查线程池是否已经 shutdown
        if (GlobalThreadPool::is_shutdown_requested()) {
            // 如果已经 shutdown，直接恢复协程而不执行 sleep
            if (h && !h.done()) {
                h.resume();
            }
            return;
        }
        
        // 在线程池中延迟执行
        try {
            GlobalThreadPool::get().enqueue_void([h, duration = duration_]() {
                std::this_thread::sleep_for(duration);
                if (h && !h.done()) {
                    h.resume();
                }
            });
        } catch (const std::exception&) {
            // 如果线程池已关闭，直接恢复协程而不执行 sleep
            if (h && !h.done()) {
                h.resume();
            }
        }
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

} // namespace flowcoro
