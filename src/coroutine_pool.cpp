#include "flowcoro/core.h"
#include "flowcoro/thread_pool.h"
#include "flowcoro/lockfree.h"
#include <iostream>
#include <iomanip>
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <mutex>
#include <condition_variable>
#include <limits>

// CPU亲和性支持
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace flowcoro {

// ==========================================
// 单个协程调度器 - 每个调度器独立运行在自己的线程中
// ==========================================

class CoroutineScheduler {
private:
    size_t scheduler_id_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    
    // 无锁协程队列 - 高性能无锁实现
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    // 精确的队列长度统计
    std::atomic<size_t> queue_size_{0};
    
    // 统计信息
    std::atomic<size_t> total_coroutines_{0};
    std::atomic<size_t> completed_coroutines_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 条件变量用于高效等待
    mutable std::mutex cv_mutex_;
    mutable std::condition_variable cv_;
    
    // CPU亲和性设置
    void set_cpu_affinity() {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        // 将调度器绑定到特定CPU核心，避免线程迁移
        CPU_SET(scheduler_id_ % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    }

    void worker_loop() {
        // 设置CPU亲和性优化多核性能
        set_cpu_affinity();
        
        const size_t BATCH_SIZE = 256; // 进一步增加批处理大小
        std::vector<std::coroutine_handle<>> batch;
        batch.reserve(BATCH_SIZE);
        
        // 延迟获取管理器和负载均衡器，避免初始化竞态
        auto* manager_ptr = &flowcoro::CoroutineManager::get_instance();
        auto* load_balancer_ptr = &manager_ptr->get_load_balancer();
        
        // 自适应等待策略 - 优化版本
        auto wait_duration = std::chrono::nanoseconds(100); // 更精细的等待控制
        constexpr auto max_wait = std::chrono::microseconds(500);
        size_t empty_iterations = 0;
        
        // 预取优化：用于减少内存访问延迟
        std::coroutine_handle<> prefetch_handle = nullptr;
        
        while (!stop_flag_.load(std::memory_order_relaxed)) {
            batch.clear();
            
            // 批量提取协程 - 使用无锁队列，带预取
            std::coroutine_handle<> handle;
            if (prefetch_handle) {
                batch.push_back(prefetch_handle);
                prefetch_handle = nullptr;
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
            }
            
            while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
                
                // 预取下一个协程的内存
                if (batch.size() < BATCH_SIZE - 1) {
                    __builtin_prefetch(handle.address(), 0, 3);
                }
            }
            
            // 如果没有任务，使用更精细的自适应等待策略
            if (__builtin_expect(batch.empty(), false)) [[unlikely]] {
                empty_iterations++;
                
                // 分级等待策略：spin -> yield -> sleep -> condition_variable
                if (empty_iterations < 64) {
                    // Level 1: 纯自旋等待（最快响应）
                    for (int i = 0; i < 64; ++i) {
                        if (!coroutine_queue_.empty()) break;
                        __builtin_ia32_pause(); // x86 pause instruction
                    }
                } else if (empty_iterations < 256) {
                    // Level 2: yield to other threads
                    std::this_thread::yield();
                } else if (empty_iterations < 1024) {
                    // Level 3: 短暂休眠
                    std::this_thread::sleep_for(wait_duration);
                    wait_duration = std::min(wait_duration * 2, std::chrono::duration_cast<std::chrono::nanoseconds>(max_wait));
                } else {
                    // Level 4: 使用条件变量等待，避免CPU浪费
                    std::unique_lock<std::mutex> lock(cv_mutex_);
                    cv_.wait_for(lock, max_wait, [this] { 
                        return stop_flag_.load(std::memory_order_relaxed) || queue_size_.load(std::memory_order_relaxed) > 0; 
                    });
                }
                continue;
            }
            
            // 重置等待策略
            empty_iterations = 0;
            wait_duration = std::chrono::microseconds(10);
            
            // 批量执行协程 - 添加适当的异常处理
            for (auto handle : batch) {
                if (handle && !handle.done() && !stop_flag_.load()) {
                    // 增强的安全检查
                    void* addr = handle.address();
                    if (!addr) {
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                        continue;
                    }
                    
                    // 检查地址合理性
                    uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);
                    if (addr_val < 0x1000 || addr_val == 0xffffffffffffffff) {
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                        continue;
                    }
                    
                    try {
                        handle.resume();
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        
                        // 通知负载均衡器任务完成
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    } catch (const std::exception& e) {
                        // 记录异常但不退出线程，确保线程池稳定性
                        std::cerr << "协程执行异常 (调度器 " << scheduler_id_ << "): " 
                                  << e.what() << std::endl;
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    } catch (...) {
                        // 处理未知异常
                        std::cerr << "协程执行未知异常 (调度器 " << scheduler_id_ << ")" << std::endl;
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    }
                }
            }
            
            // 实时更新负载均衡器
            load_balancer_ptr->update_load(scheduler_id_, queue_size_.load());
        }
    }

public:
    CoroutineScheduler(size_t id) 
        : scheduler_id_(id), start_time_(std::chrono::steady_clock::now()) {
        // 不在构造函数中启动线程，避免初始化时的竞态条件
    }
    
    void start() {
        if (!worker_thread_.joinable()) {
            worker_thread_ = std::thread(&CoroutineScheduler::worker_loop, this);
        }
    }
    
    ~CoroutineScheduler() {
        stop();
    }
    
    void stop() {
        if (!stop_flag_.exchange(true)) {
            // 唤醒等待的worker线程
            cv_.notify_all();
            
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            
            // 清理剩余协程 - 使用无锁队列
            std::coroutine_handle<> handle;
            while (coroutine_queue_.dequeue(handle)) {
                if (handle && !handle.done()) {
                    try {
                        handle.destroy();
                    } catch (...) {
                        // 忽略销毁时的异常
                    }
                }
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }
    
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;
        
        // 增强的安全检查
        void* addr = handle.address();
        if (!addr) return;
        
        // 检查地址合理性
        uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);
        if (addr_val < 0x1000 || addr_val == 0xffffffffffffffff) {
            return;
        }
        
        total_coroutines_.fetch_add(1, std::memory_order_relaxed);
        
        // 使用无锁队列添加协程
        coroutine_queue_.enqueue(handle);
        
        // 精确更新队列大小
        queue_size_.fetch_add(1, std::memory_order_relaxed);
        
        // 唤醒等待的worker线程
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
        }
        cv_.notify_one();
        
        // 实时更新负载均衡器的队列大小信息
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.update_load(scheduler_id_, queue_size_.load());
    }
    
    size_t get_queue_size() const {
        return queue_size_.load(std::memory_order_relaxed);
    }
    
    size_t get_total_coroutines() const { return total_coroutines_.load(); }
    size_t get_completed_coroutines() const { return completed_coroutines_.load(); }
    size_t get_scheduler_id() const { return scheduler_id_; }
    
    std::chrono::milliseconds get_uptime() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    }
};

// ==========================================
// 多调度器协程池 - 管理12个独立的协程调度器
// ==========================================

class CoroutinePool {
private:
    static std::atomic<CoroutinePool*> instance_;
    
    // 动态根据CPU核心数确定调度器数量
    const size_t NUM_SCHEDULERS;
    
    // 动态数量的独立协程调度器
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 后台线程池 - 处理CPU密集型任务
    std::unique_ptr<lockfree::ThreadPool> thread_pool_;
    
    std::atomic<bool> stop_flag_{false};
    
    // 统计信息
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    CoroutinePool()
        : NUM_SCHEDULERS(1), // 固定使用1个调度器 - 性能最优配置
          start_time_(std::chrono::steady_clock::now()) {
        // 根据CPU核心数初始化调度器
        schedulers_.reserve(NUM_SCHEDULERS);
        for (size_t i = 0; i < NUM_SCHEDULERS; ++i) {
            schedulers_.emplace_back(std::make_unique<CoroutineScheduler>(i));
        }
        
        // 智能线程池配置：优化并发性能
        size_t thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 8; // 备用值

        // 优化配置：适中的线程数以获得最佳性能/开销比
        thread_count = std::max(thread_count, static_cast<size_t>(8)); // 至少8个线程
        thread_count = std::min(thread_count, static_cast<size_t>(24)); // 最多24个线程，减少调度开销

        thread_pool_ = std::make_unique<lockfree::ThreadPool>(thread_count);

        // 仅在需要时输出启动信息
        static bool first_init = true;
        if (first_init) {
            std::cout << "FlowCoro智能协程池启动 - " << NUM_SCHEDULERS 
                      << "个协程调度器 + " << thread_count 
                      << "个工作线程 (智能负载均衡)" << std::endl;
            first_init = false;
        }

        // 在完全初始化后再启动调度器，避免初始化时的竞态条件
        for (auto& scheduler : schedulers_) {
            scheduler->start();
        }
        
        // 初始化智能负载均衡器
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.set_scheduler_count(NUM_SCHEDULERS);
    }

    ~CoroutinePool() {
        stop_flag_.store(true);
        
        // 停止所有调度器
        for (auto& scheduler : schedulers_) {
            if (scheduler) {
                scheduler->stop();
            }
        }
        
        schedulers_.clear();
        std::cout << "FlowCoro自适应协程池关闭 (" << NUM_SCHEDULERS << "个调度器)" << std::endl;
    }

    static CoroutinePool& get_instance() {
        CoroutinePool* current = instance_.load(std::memory_order_acquire);
        if (current == nullptr) {
            static std::mutex init_mutex;
            std::lock_guard<std::mutex> lock(init_mutex);
            current = instance_.load(std::memory_order_relaxed);
            if (current == nullptr) {
                current = new CoroutinePool();
                instance_.store(current, std::memory_order_release);
            }
        }
        return *current;
    }

    static void shutdown() {
        CoroutinePool* current = instance_.exchange(nullptr, std::memory_order_acq_rel);
        if (current) {
            delete current;
        }
    }

    // 协程调度 - 使用智能负载均衡分配到调度器
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;

        // 使用智能负载均衡：选择最优调度器
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        
        size_t scheduler_index = load_balancer.select_scheduler();
        
        // 分配给对应的调度器
        schedulers_[scheduler_index]->schedule_coroutine(handle);
    }

    // CPU密集型任务 - 提交到后台线程池 (增强异常处理)
    void schedule_task(std::function<void()> task) {
        if (stop_flag_.load()) return;

        total_tasks_.fetch_add(1, std::memory_order_relaxed);

        // 将任务提交到后台线程池执行 - 增强异常处理
        thread_pool_->enqueue([this, task = std::move(task)]() {
            try {
                task();
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                std::cerr << "任务执行异常: " << e.what() << std::endl;
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                std::cerr << "任务执行未知异常" << std::endl;
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 驱动协程池 - 增强版本：主动处理队列并推进协程执行
    void drive() {
        if (stop_flag_.load(std::memory_order_relaxed)) return;
        
        // 获取协程管理器并处理其队列
        auto& manager = flowcoro::CoroutineManager::get_instance();
        
        // 1. 处理定时器队列 - 将到期的定时器移到就绪队列
        manager.process_timer_queue();
        
        // 2. 处理就绪队列 - 立即执行就绪的协程
        manager.process_ready_queue();
        
        // 3. 给调度器线程一些时间处理
        // 这对于异步任务很重要
        for (auto& scheduler : schedulers_) {
            if (scheduler) {
                std::this_thread::yield();
            }
        }
        
        // 4. 处理待销毁的协程 - 清理资源
        manager.process_pending_tasks();
    }

    // 获取统计信息
    struct PoolStats {
        size_t num_schedulers;
        size_t thread_pool_workers;
        size_t pending_coroutines;
        size_t total_coroutines;
        size_t completed_coroutines;
        size_t total_tasks;
        size_t completed_tasks;
        double coroutine_completion_rate;
        double task_completion_rate;
        std::chrono::milliseconds uptime;
    };

    PoolStats get_stats() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

        size_t pending = 0;
        size_t total_cor = 0;
        size_t completed_cor = 0;
        
        // 汇总所有调度器的统计信息
        for (const auto& scheduler : schedulers_) {
            if (scheduler) {
                pending += scheduler->get_queue_size();
                total_cor += scheduler->get_total_coroutines();
                completed_cor += scheduler->get_completed_coroutines();
            }
        }

        size_t total_task = total_tasks_.load();
        size_t completed_task = completed_tasks_.load();

        return PoolStats{
            NUM_SCHEDULERS, // num_schedulers
            std::max(std::thread::hardware_concurrency(), static_cast<unsigned int>(32)), // thread_pool_workers (显示实际线程数)
            pending, // pending_coroutines
            total_cor, // total_coroutines
            completed_cor, // completed_coroutines
            total_task, // total_tasks
            completed_task, // completed_tasks
            total_cor > 0 ? (double)completed_cor / total_cor : 0.0, // coroutine_completion_rate
            total_task > 0 ? (double)completed_task / total_task : 0.0, // task_completion_rate
            uptime
        };
    }

    void print_stats() const {
        auto stats = get_stats();

        std::cout << "\n=== FlowCoro 多调度器协程池统计 ===" << std::endl;
        std::cout << " 运行时间: " << stats.uptime.count() << " ms" << std::endl;
        std::cout << " 架构模式: " << stats.num_schedulers << "个独立协程调度器 + 后台线程池" << std::endl;
        std::cout << " 工作线程: " << stats.thread_pool_workers << " 个" << std::endl;
        std::cout << " 待处理协程: " << stats.pending_coroutines << std::endl;
        std::cout << " 总协程数: " << stats.total_coroutines << std::endl;
        std::cout << " 完成协程: " << stats.completed_coroutines << std::endl;
        std::cout << " 总任务数: " << stats.total_tasks << std::endl;
        std::cout << " 完成任务: " << stats.completed_tasks << std::endl;
        std::cout << " 协程完成率: " << std::fixed << std::setprecision(1)
                  << (stats.coroutine_completion_rate * 100) << "%" << std::endl;
        std::cout << " 任务完成率: " << std::fixed << std::setprecision(1)
                  << (stats.task_completion_rate * 100) << "%" << std::endl;
        
        // 显示各个调度器的详细信息
        std::cout << "\n--- 调度器详情 ---" << std::endl;
        for (const auto& scheduler : schedulers_) {
            if (scheduler) {
                std::cout << "调度器 #" << scheduler->get_scheduler_id() 
                          << " - 队列: " << scheduler->get_queue_size()
                          << ", 总数: " << scheduler->get_total_coroutines()
                          << ", 完成: " << scheduler->get_completed_coroutines()
                          << ", 运行时间: " << scheduler->get_uptime().count() << "ms" << std::endl;
            }
        }
        
        // 显示负载均衡统计
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        auto load_stats = load_balancer.get_load_stats();
        
        std::cout << "\n--- 负载均衡详情 ---" << std::endl;
        for (const auto& stat : load_stats) {
            std::cout << "调度器 #" << stat.scheduler_id 
                      << " - 当前队列: " << stat.queue_load
                      << ", 历史处理: " << stat.total_processed
                      << ", 负载评分: " << std::fixed << std::setprecision(2) << stat.load_score
                      << std::endl;
        }
        
        std::cout << "===============================" << std::endl;
    }
};

// 静态成员定义
std::atomic<CoroutinePool*> CoroutinePool::instance_{nullptr};

// ==========================================
// 全局接口函数 - 协程池驱动接口
// ==========================================

// 协程调度接口
void schedule_coroutine_enhanced(std::coroutine_handle<> handle) {
    CoroutinePool::get_instance().schedule_coroutine(handle);
}

// 任务调度接口 (提交到线程池)
void schedule_task_enhanced(std::function<void()> task) {
    CoroutinePool::get_instance().schedule_task(std::move(task));
}

// 驱动协程池 - 需要在主线程中定期调用
void drive_coroutine_pool() {
    CoroutinePool::get_instance().drive();
}

// 统计信息接口
void print_pool_stats() {
    CoroutinePool::get_instance().print_stats();
}

// 关闭接口
void shutdown_coroutine_pool() {
    CoroutinePool::shutdown();
}

// 运行协程直到完成的高性能实现 - 改进版本，减少CPU浪费
void run_until_complete(Task<void>& task) {
    std::atomic<bool> completed{false};

    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();

    // 创建完成回调 - 改进版本，添加异常处理
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            std::cerr << "协程执行异常: " << e.what() << std::endl;
            completed.store(true, std::memory_order_release);
        } catch (...) {
            std::cerr << "协程执行未知异常" << std::endl;
            completed.store(true, std::memory_order_release);
        }
    }();

    // 启动任务
    manager.schedule_resume(completion_task.handle);

    // 使用自适应等待策略，减少CPU浪费
    auto wait_duration = std::chrono::microseconds(1);
    constexpr auto max_wait = std::chrono::milliseconds(1);
    size_t iterations = 0;
    
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 驱动协程调度
        
        iterations++;
        
        // 自适应等待策略
        if (iterations < 100) {
            // 前100次快速轮询
            std::this_thread::yield();
        } else if (iterations < 1000) {
            // 中等等待
            std::this_thread::sleep_for(wait_duration);
            wait_duration = std::min(wait_duration * 2, std::chrono::microseconds(100));
        } else {
            // 长时间等待，减少CPU使用
            std::this_thread::sleep_for(max_wait);
        }
    }

    // 最终清理
    manager.drive();
}

template<typename T>
void run_until_complete(Task<T>& task) {
    std::atomic<bool> completed{false};

    // 获取协程管理器
    auto& manager = CoroutineManager::get_instance();

    // 创建完成回调 - 改进版本，添加异常处理
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            std::cerr << "协程执行异常: " << e.what() << std::endl;
            completed.store(true, std::memory_order_release);
        } catch (...) {
            std::cerr << "协程执行未知异常" << std::endl;
            completed.store(true, std::memory_order_release);
        }
    }();

    // 启动任务
    manager.schedule_resume(completion_task.handle);

    // 使用自适应等待策略，减少CPU浪费
    auto wait_duration = std::chrono::microseconds(1);
    constexpr auto max_wait = std::chrono::milliseconds(1);
    size_t iterations = 0;
    
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 驱动协程调度
        
        iterations++;
        
        // 自适应等待策略
        if (iterations < 100) {
            // 前100次快速轮询
            std::this_thread::yield();
        } else if (iterations < 1000) {
            // 中等等待
            std::this_thread::sleep_for(wait_duration);
            wait_duration = std::min(wait_duration * 2, std::chrono::microseconds(100));
        } else {
            // 长时间等待，减少CPU使用
            std::this_thread::sleep_for(max_wait);
        }
    }

    // 最终清理
    manager.drive();
}

// 显式实例化常用类型
template void run_until_complete<int>(Task<int>&);
template void run_until_complete<void>(Task<void>&);

} // namespace flowcoro
