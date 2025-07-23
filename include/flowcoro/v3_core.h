#pragma once
#include <coroutine>
#include <memory>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <shared_mutex>
#include <chrono>
#include <unordered_map>
#include <deque>
#include <utility>
#include <iostream>
#include <iomanip>
#include <string>
#include <functional>
#include <optional>
#include <random>

namespace flowcoro::v3 {

// ==========================================
// FlowCoro 3.0 - 底层架构升级
// 借鉴 Folly 和 CppCoro 的最佳实践
// ==========================================

/**
 * @brief 协程栈内存池 - 基于 Folly IndexedMemPool 设计
 * 
 * 特性：
 * - 预分配栈空间，避免运行时分配
 * - 栈空间复用，减少内存碎片
 * - 线程安全的分配和释放
 * - 自动内存回收策略
 */
class CoroutineStackPool {
public:
    static constexpr size_t DEFAULT_STACK_SIZE = 64 * 1024;  // 64KB 栈
    static constexpr size_t DEFAULT_POOL_SIZE = 1000;
    
    struct StackBlock {
        alignas(64) char data[DEFAULT_STACK_SIZE];
        std::atomic<bool> in_use{false};
        std::chrono::steady_clock::time_point last_used;
        size_t block_id;
        
        void reset() {
            in_use.store(false, std::memory_order_release);
            last_used = std::chrono::steady_clock::now();
        }
    };
    
private:
    std::vector<std::unique_ptr<StackBlock>> stack_blocks_;
    std::queue<size_t> available_indices_;
    mutable std::mutex mutex_;
    std::atomic<size_t> allocated_count_{0};
    std::atomic<size_t> peak_usage_{0};
    
    // 内存清理线程
    std::thread cleanup_thread_;
    std::atomic<bool> shutdown_requested_{false};
    std::chrono::seconds idle_timeout_{30};  // 30秒空闲超时
    
public:
    explicit CoroutineStackPool(size_t initial_size = DEFAULT_POOL_SIZE);
    ~CoroutineStackPool();
    
    StackBlock* acquire_stack();
    void release_stack(StackBlock* block);
    
    struct PoolStats {
        size_t total_stacks;
        size_t available_stacks;
        size_t allocated_stacks;
        size_t peak_usage;
        double utilization_rate;
    };
    
    PoolStats get_stats() const;
    
private:
    void cleanup_worker();
    void expand_pool(size_t additional_size);
};

/**
 * @brief 工作窃取调度器 - 基于 CppCoro static_thread_pool 设计
 * 
 * 特性：
 * - 每个线程独立的本地队列 (LIFO)
 * - 全局共享队列 (FIFO)
 * - 工作窃取算法减少线程竞争
 * - 自适应线程休眠和唤醒
 */
class WorkStealingScheduler {
public:
    class Task {
    public:
        std::coroutine_handle<> handle;
        std::chrono::steady_clock::time_point deadline;
        int priority = 0;
        Task* next = nullptr;
        
        Task(std::coroutine_handle<> h, int prio = 0) 
            : handle(h), priority(prio) {}
    };
    
private:
    // 线程本地队列
    struct ThreadLocalQueue {
        static constexpr size_t QUEUE_SIZE = 4096;
        
        std::atomic<Task*> local_queue[QUEUE_SIZE];
        alignas(64) std::atomic<size_t> head{0};
        alignas(64) std::atomic<size_t> tail{0};
        std::mutex steal_mutex;
        
        bool try_push(Task* task);
        Task* try_pop();
        Task* try_steal();
    };
    
    // 全局队列
    struct GlobalQueue {
        std::queue<Task*> tasks;
        std::mutex mutex;
        
        void push(Task* task);
        Task* try_pop();
        bool empty() const;
    };
    
    const size_t num_threads_;
    std::vector<std::unique_ptr<ThreadLocalQueue>> thread_queues_;
    GlobalQueue global_queue_;
    
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_requested_{false};
    
    // 线程休眠控制
    std::atomic<size_t> sleeping_thread_count_{0};
    std::unique_ptr<std::condition_variable[]> thread_cvs_;
    std::unique_ptr<std::mutex[]> thread_mutexes_;
    std::unique_ptr<std::atomic<bool>[]> thread_sleeping_;
    
    // 线程本地存储
    static thread_local size_t thread_index_;
    static thread_local WorkStealingScheduler* current_scheduler_;
    
public:
    explicit WorkStealingScheduler(size_t num_threads = std::thread::hardware_concurrency());
    ~WorkStealingScheduler();
    
    void schedule(std::coroutine_handle<> handle, int priority = 0);
    void schedule_at(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point when);
    
    size_t get_thread_count() const { return num_threads_; }
    bool is_current_thread_worker() const;
    
private:
    void worker_thread_main(size_t thread_id);
    Task* try_get_task(size_t thread_id);
    void wake_sleeping_thread();
    void wait_for_work(size_t thread_id);
};

/**
 * @brief 协程句柄管理器 - 生命周期和内存安全
 * 
 * 特性：
 * - 智能协程句柄包装
 * - 自动内存回收
 * - 异常安全保证
 * - 调试和监控支持
 */
class CoroutineHandleManager {
public:
    struct ManagedHandle {
        std::coroutine_handle<> handle;
        CoroutineStackPool::StackBlock* stack_block = nullptr;
        std::chrono::steady_clock::time_point created_at;
        std::atomic<bool> destroyed{false};
        std::string debug_name;
        
        ManagedHandle(std::coroutine_handle<> h, CoroutineStackPool::StackBlock* stack = nullptr)
            : handle(h), stack_block(stack), created_at(std::chrono::steady_clock::now()) {}
        
        ~ManagedHandle();
    };
    
private:
    std::unordered_map<void*, std::unique_ptr<ManagedHandle>> handles_;
    mutable std::shared_mutex handles_mutex_;
    
    CoroutineStackPool& stack_pool_;
    WorkStealingScheduler& scheduler_;
    
public:
    CoroutineHandleManager(CoroutineStackPool& stack_pool, WorkStealingScheduler& scheduler)
        : stack_pool_(stack_pool), scheduler_(scheduler) {}
    
    ManagedHandle* register_handle(std::coroutine_handle<> handle, const std::string& debug_name = "");
    void unregister_handle(std::coroutine_handle<> handle);
    
    size_t get_active_count() const;
    std::vector<std::string> get_debug_info() const;
};

/**
 * @brief FlowCoro 3.0 运行时 - 统一管理所有组件
 * 
 * 特性：
 * - 单例模式，全局访问
 * - 组件生命周期管理
 * - 性能监控和统计
 * - 配置管理
 */
class FlowCoroRuntime {
public:
    struct Config {
        size_t num_scheduler_threads = std::thread::hardware_concurrency();
        size_t initial_stack_pool_size = 1000;
        size_t stack_size = CoroutineStackPool::DEFAULT_STACK_SIZE;
        std::chrono::seconds stack_idle_timeout{30};
        bool enable_debug_mode = false;
        bool enable_performance_monitoring = true;
    };
    
    static const Config& default_config() {
        static Config config{};
        return config;
    }
    
    struct RuntimeStats {
        CoroutineStackPool::PoolStats stack_pool_stats;
        size_t active_coroutines;
        size_t total_scheduled_tasks;
        size_t completed_tasks;
        double average_task_duration_ms;
        std::chrono::steady_clock::time_point runtime_start_time;
    };
    
private:
    Config config_;
    
    std::unique_ptr<CoroutineStackPool> stack_pool_;
    std::unique_ptr<WorkStealingScheduler> scheduler_;
    std::unique_ptr<CoroutineHandleManager> handle_manager_;
    
    // 性能统计
    std::atomic<size_t> total_scheduled_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 单例相关
    static std::unique_ptr<FlowCoroRuntime> instance_;
    static std::once_flag init_flag_;
    
public:
    explicit FlowCoroRuntime(const Config& config = default_config());
    ~FlowCoroRuntime();
    
    // 单例访问
    static FlowCoroRuntime& get_instance();
    static void initialize(const Config& config = default_config());
    static void shutdown();
    
    // 核心接口
    void schedule_coroutine(std::coroutine_handle<> handle, int priority = 0);
    void schedule_coroutine_at(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point when);
    
    // 组件访问
    CoroutineStackPool& get_stack_pool() { return *stack_pool_; }
    WorkStealingScheduler& get_scheduler() { return *scheduler_; }
    CoroutineHandleManager& get_handle_manager() { return *handle_manager_; }
    
    // 统计和监控
    RuntimeStats get_stats() const;
    void print_stats() const;
    
    // 配置管理
    const Config& get_config() const { return config_; }
    void update_config(const Config& new_config);
};

/**
 * @brief 优化的 Task<T> 实现 - 使用新的运行时
 */
template<typename T = void>
class Task {
public:
    struct promise_type {
        std::optional<T> result_;
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
        CoroutineHandleManager::ManagedHandle* managed_handle_ = nullptr;
        
        Task get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 注册到句柄管理器
            auto& runtime = FlowCoroRuntime::get_instance();
            managed_handle_ = runtime.get_handle_manager().register_handle(handle, "Task");
            
            return Task{handle};
        }
        
        std::suspend_never initial_suspend() noexcept { return {}; }
        
        std::suspend_always final_suspend() noexcept { 
            // 如果有continuation，调度它
            if (continuation_) {
                FlowCoroRuntime::get_instance().schedule_coroutine(continuation_);
            }
            return {}; 
        }
        
        template<typename U>
        void return_value(U&& value) {
            result_ = std::forward<U>(value);
        }
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        ~promise_type() {
            // 从句柄管理器注销
            if (managed_handle_) {
                auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
                FlowCoroRuntime::get_instance().get_handle_manager().unregister_handle(handle);
            }
        }
    };
    
private:
    std::coroutine_handle<promise_type> handle_;
    
public:
    explicit Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // 移动语义
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    
    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    // Awaitable 接口
    bool await_ready() const { return handle_.done(); }
    
    void await_suspend(std::coroutine_handle<> continuation) {
        handle_.promise().continuation_ = continuation;
    }
    
    T await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*handle_.promise().result_);
        }
    }
    
    // 同步等待
    T get() {
        while (!handle_.done()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return await_resume();
    }
    
    bool ready() const { return handle_.done(); }
};

// void 特化
template<>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
        CoroutineHandleManager::ManagedHandle* managed_handle_ = nullptr;
        
        Task get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            auto& runtime = FlowCoroRuntime::get_instance();
            managed_handle_ = runtime.get_handle_manager().register_handle(handle, "Task<void>");
            return Task{handle};
        }
        
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { 
            if (continuation_) {
                FlowCoroRuntime::get_instance().schedule_coroutine(continuation_);
            }
            return {}; 
        }
        
        void return_void() {}
        void unhandled_exception() { exception_ = std::current_exception(); }
        
        ~promise_type() {
            if (managed_handle_) {
                auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
                FlowCoroRuntime::get_instance().get_handle_manager().unregister_handle(handle);
            }
        }
    };
    
private:
    std::coroutine_handle<promise_type> handle_;
    
public:
    explicit Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ~Task() { if (handle_) handle_.destroy(); }
    
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    bool await_ready() const { return handle_.done(); }
    void await_suspend(std::coroutine_handle<> continuation) {
        handle_.promise().continuation_ = continuation;
    }
    void await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }
    
    void get() {
        while (!handle_.done()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        await_resume();
    }
    
    bool ready() const { return handle_.done(); }
};

} // namespace flowcoro::v3
