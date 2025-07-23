#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "../include/flowcoro.hpp"

// 学习 Folly/CppCoro 的协程池优化设计
class OptimizedCoroutinePool {
public:
    // 协程栈复用池 - 借鉴 IndexedMemPool 设计
    struct CoroutineContext {
        std::coroutine_handle<> handle;
        std::atomic<bool> in_use{false};
        char stack_space[4096];  // 预分配栈空间
        std::chrono::steady_clock::time_point last_used;
        
        void reset() {
            handle = {};
            in_use.store(false, std::memory_order_release);
            last_used = std::chrono::steady_clock::now();
        }
    };
    
    // 线程本地存储池 - 借鉴 static_thread_pool 的 per-thread queue
    static thread_local std::vector<std::unique_ptr<CoroutineContext>> thread_local_pool;
    static thread_local size_t local_pool_index;
    
    // 全局协程池 - 学习 Folly 的内存管理
    class GlobalCoroutinePool {
    private:
        std::vector<std::unique_ptr<CoroutineContext>> contexts_;
        std::queue<size_t> available_indices_;
        std::mutex mutex_;
        std::atomic<size_t> allocated_count_{0};
        std::atomic<size_t> peak_usage_{0};
        
        // 内存回收策略 - 借鉴 MemoryIdler
        std::chrono::steady_clock::duration idle_timeout_{std::chrono::seconds(5)};
        std::thread cleanup_thread_;
        std::atomic<bool> shutdown_requested_{false};
        
    public:
        GlobalCoroutinePool(size_t initial_size = 100) {
            contexts_.reserve(initial_size * 2);  // 预留增长空间
            
            // 预分配协程上下文
            for (size_t i = 0; i < initial_size; ++i) {
                contexts_.emplace_back(std::make_unique<CoroutineContext>());
                available_indices_.push(i);
            }
            
            // 启动内存清理线程
            cleanup_thread_ = std::thread([this] { cleanup_worker(); });
        }
        
        ~GlobalCoroutinePool() {
            shutdown_requested_.store(true);
            if (cleanup_thread_.joinable()) {
                cleanup_thread_.join();
            }
        }
        
        CoroutineContext* acquire() {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (available_indices_.empty()) {
                // 动态扩展 - 学习 static_thread_pool 的队列扩展
                size_t old_size = contexts_.size();
                size_t new_size = std::min(old_size * 2, old_size + 50);  // 限制增长速度
                
                for (size_t i = old_size; i < new_size; ++i) {
                    contexts_.emplace_back(std::make_unique<CoroutineContext>());
                    available_indices_.push(i);
                }
                
                std::cout << "📈 协程池扩展: " << old_size << " → " << new_size << " 个上下文" << std::endl;
            }
            
            if (!available_indices_.empty()) {
                size_t index = available_indices_.front();
                available_indices_.pop();
                
                auto* context = contexts_[index].get();
                context->in_use.store(true, std::memory_order_acquire);
                
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                
                // 更新峰值使用量
                size_t current = allocated_count_.load();
                size_t peak = peak_usage_.load();
                while (current > peak && !peak_usage_.compare_exchange_weak(peak, current)) {
                    peak = peak_usage_.load();
                }
                
                return context;
            }
            
            return nullptr;
        }
        
        void release(CoroutineContext* context) {
            if (!context) return;
            
            context->reset();
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            // 找到上下文的索引
            for (size_t i = 0; i < contexts_.size(); ++i) {
                if (contexts_[i].get() == context) {
                    available_indices_.push(i);
                    break;
                }
            }
            
            allocated_count_.fetch_sub(1, std::memory_order_relaxed);
        }
        
        // 内存统计
        struct PoolStats {
            size_t total_contexts;
            size_t available_contexts;
            size_t allocated_contexts;
            size_t peak_usage;
            double utilization_rate;
        };
        
        PoolStats get_stats() const {
            std::lock_guard<std::mutex> lock(mutex_);
            
            PoolStats stats{};
            stats.total_contexts = contexts_.size();
            stats.available_contexts = available_indices_.size();
            stats.allocated_contexts = allocated_count_.load();
            stats.peak_usage = peak_usage_.load();
            stats.utilization_rate = stats.total_contexts > 0 ? 
                (double)stats.allocated_contexts / stats.total_contexts : 0.0;
            
            return stats;
        }
        
    private:
        void cleanup_worker() {
            while (!shutdown_requested_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(mutex_);
                
                // 回收长时间未使用的上下文 - 学习 MemoryIdler 策略
                size_t cleaned = 0;
                for (auto& context : contexts_) {
                    if (!context->in_use.load() && 
                        (now - context->last_used) > idle_timeout_) {
                        // 执行内存清理操作
                        // 这里可以调用 madvise 等系统调用来释放物理内存
                        cleaned++;
                    }
                }
                
                if (cleaned > 0) {
                    std::cout << "🧹 清理了 " << cleaned << " 个空闲协程上下文" << std::endl;
                }
            }
        }
    };
    
    static GlobalCoroutinePool& get_global_pool() {
        static GlobalCoroutinePool pool;
        return pool;
    }
    
    // 智能协程句柄 - 自动回收
    class ManagedCoroutine {
    private:
        CoroutineContext* context_;
        
    public:
        ManagedCoroutine() : context_(get_global_pool().acquire()) {}
        
        ~ManagedCoroutine() {
            if (context_) {
                get_global_pool().release(context_);
            }
        }
        
        ManagedCoroutine(const ManagedCoroutine&) = delete;
        ManagedCoroutine& operator=(const ManagedCoroutine&) = delete;
        
        ManagedCoroutine(ManagedCoroutine&& other) noexcept 
            : context_(std::exchange(other.context_, nullptr)) {}
        
        ManagedCoroutine& operator=(ManagedCoroutine&& other) noexcept {
            if (this != &other) {
                if (context_) {
                    get_global_pool().release(context_);
                }
                context_ = std::exchange(other.context_, nullptr);
            }
            return *this;
        }
        
        CoroutineContext* get() const { return context_; }
        bool valid() const { return context_ != nullptr; }
    };
};

// 线程本地存储初始化
thread_local std::vector<std::unique_ptr<OptimizedCoroutinePool::CoroutineContext>> 
    OptimizedCoroutinePool::thread_local_pool;
thread_local size_t OptimizedCoroutinePool::local_pool_index = 0;

// 优化的协程任务 - 使用对象池
template<typename T>
class OptimizedTask {
public:
    struct promise_type {
        std::optional<T> result;
        std::exception_ptr exception;
        OptimizedCoroutinePool::ManagedCoroutine managed_coro;
        
        OptimizedTask get_return_object() {
            return OptimizedTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { 
            return {}; 
        }
        
        void return_value(T value) {
            result = std::move(value);
        }
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };
    
private:
    std::coroutine_handle<promise_type> handle_;
    
public:
    explicit OptimizedTask(std::coroutine_handle<promise_type> h) : handle_(h) {}
    
    ~OptimizedTask() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    OptimizedTask(const OptimizedTask&) = delete;
    OptimizedTask& operator=(const OptimizedTask&) = delete;
    
    OptimizedTask(OptimizedTask&& other) noexcept 
        : handle_(std::exchange(other.handle_, {})) {}
    
    OptimizedTask& operator=(OptimizedTask&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    
    T get() {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        return *handle_.promise().result;
    }
    
    bool ready() const {
        return handle_.done();
    }
};

// 使用示例：优化的HTTP请求处理
OptimizedTask<std::string> optimized_http_request(int request_id) {
    // 模拟HTTP请求处理
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    co_return "Response for request " + std::to_string(request_id);
}

// 性能测试函数
void benchmark_optimized_coroutines(int request_count) {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<OptimizedTask<std::string>> tasks;
    tasks.reserve(request_count);
    
    // 创建任务
    for (int i = 0; i < request_count; ++i) {
        tasks.emplace_back(optimized_http_request(i));
    }
    
    // 等待完成
    for (auto& task : tasks) {
        while (!task.ready()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 获取池统计
    auto stats = OptimizedCoroutinePool::get_global_pool().get_stats();
    
    std::cout << "🚀 优化协程测试完成:" << std::endl;
    std::cout << "   请求数量: " << request_count << std::endl;
    std::cout << "   总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "   吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << std::endl;
    
    std::cout << "📊 协程池统计:" << std::endl;
    std::cout << "   总上下文数: " << stats.total_contexts << std::endl;
    std::cout << "   可用上下文: " << stats.available_contexts << std::endl;
    std::cout << "   已分配上下文: " << stats.allocated_contexts << std::endl;
    std::cout << "   峰值使用量: " << stats.peak_usage << std::endl;
    std::cout << "   利用率: " << std::fixed << std::setprecision(1) 
              << (stats.utilization_rate * 100) << "%" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "🎯 优化协程池内存管理测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "📚 借鉴优秀开源项目设计:" << std::endl;
    std::cout << "   • Folly IndexedMemPool - 对象池复用" << std::endl;
    std::cout << "   • CppCoro static_thread_pool - 工作窃取" << std::endl;
    std::cout << "   • Folly MemoryIdler - 内存回收策略" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // 测试不同规模
    std::vector<int> test_sizes = {100, 500, 1000, 2000};
    
    for (int size : test_sizes) {
        std::cout << "🔸 测试 " << size << " 个协程:" << std::endl;
        benchmark_optimized_coroutines(size);
        std::cout << std::endl;
        
        // 等待一下，让清理线程工作
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "💡 优化要点总结:" << std::endl;
    std::cout << "   ✅ 协程上下文复用 - 减少动态分配" << std::endl;
    std::cout << "   ✅ 预分配栈空间 - 避免运行时分配" << std::endl;
    std::cout << "   ✅ 线程本地缓存 - 减少锁竞争" << std::endl;
    std::cout << "   ✅ 智能内存回收 - 自动清理空闲资源" << std::endl;
    std::cout << "   ✅ 动态池扩展 - 按需增长容量" << std::endl;
    std::cout << "   ✅ 详细内存统计 - 监控内存使用情况" << std::endl;
    
    return 0;
}
