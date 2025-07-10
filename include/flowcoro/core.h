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
    static std::unique_ptr<lockfree::ThreadPool> instance_;
    static std::once_flag init_flag_;
    
    static void init() {
        instance_ = std::make_unique<lockfree::ThreadPool>(std::thread::hardware_concurrency());
    }
    
public:
    static lockfree::ThreadPool& get() {
        std::call_once(init_flag_, init);
        return *instance_;
    }
    
    static void shutdown() {
        if (instance_) {
            instance_->shutdown();
            instance_.reset();
        }
    }
};

// 静态成员定义
std::unique_ptr<lockfree::ThreadPool> GlobalThreadPool::instance_;
std::once_flag GlobalThreadPool::init_flag_;

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
        T value;
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
        if (handle.promise().exception) std::rethrow_exception(handle.promise().exception);
        return std::move(handle.promise().value);
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
            
            // 在锁保护下获取等待的协程句柄
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
        return state_->value_;
    }
    
private:
    struct State {
        std::atomic<bool> ready_{false};
        T value_{};
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_; // 保护非原子类型的value_
    };
    
    std::shared_ptr<State> state_;
};

// 无锁任务调度器
class TaskScheduler {
private:
    static lockfree::Queue<std::function<void()>> task_queue_;
    
public:
    // 延迟执行任务
    template<typename F>
    static void schedule_task(F&& task) {
        task_queue_.enqueue(std::forward<F>(task));
    }
    
    // 在线程池中执行任务
    template<typename F, typename... Args>
    static auto run_async(F&& f, Args&&... args) {
        return GlobalThreadPool::get().enqueue(
            std::forward<F>(f), std::forward<Args>(args)...
        );
    }
    
    // 处理待执行的任务
    static void process_tasks() {
        std::function<void()> task;
        while (task_queue_.dequeue(task)) {
            try {
                task();
            } catch (const std::exception& e) {
                // 日志记录异常
            }
        }
    }
};

// 静态成员定义
lockfree::Queue<std::function<void()>> TaskScheduler::task_queue_;

// 协程等待器：等待一定时间
class SleepAwaiter {
public:
    explicit SleepAwaiter(std::chrono::milliseconds duration) 
        : duration_(duration) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) {
        // 在线程池中延迟执行
        GlobalThreadPool::get().enqueue_void([h, duration = duration_]() {
            std::this_thread::sleep_for(duration);
            if (h && !h.done()) {
                h.resume();
            }
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

} // namespace flowcoro
