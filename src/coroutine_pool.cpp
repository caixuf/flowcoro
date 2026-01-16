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

// CPU
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace flowcoro {

// ==========================================
//  - 
// ==========================================

class CoroutineScheduler {
private:
    size_t scheduler_id_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    
    //  - 
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    // 
    std::atomic<size_t> queue_size_{0};
    
    // 
    std::atomic<size_t> total_coroutines_{0};
    std::atomic<size_t> completed_coroutines_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 
    mutable std::mutex cv_mutex_;
    mutable std::condition_variable cv_;
    
    // CPU
    void set_cpu_affinity() {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        // CPU
        CPU_SET(scheduler_id_ % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    }

    void worker_loop() {
        // CPU
        set_cpu_affinity();
        
        const size_t BATCH_SIZE = 256; // 
        std::vector<std::coroutine_handle<>> batch;
        batch.reserve(BATCH_SIZE);
        
        // 
        auto* manager_ptr = &flowcoro::CoroutineManager::get_instance();
        auto* load_balancer_ptr = &manager_ptr->get_load_balancer();
        
        //  - 
        auto wait_duration = std::chrono::nanoseconds(100); // 
        constexpr auto max_wait = std::chrono::microseconds(500);
        size_t empty_iterations = 0;
        
        // 
        std::coroutine_handle<> prefetch_handle = nullptr;
        
        while (!stop_flag_.load(std::memory_order_relaxed)) {
            batch.clear();
            
            //  - 
            std::coroutine_handle<> handle;
            if (prefetch_handle) {
                batch.push_back(prefetch_handle);
                prefetch_handle = nullptr;
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
            }
            
            while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
                
                // 
                if (batch.size() < BATCH_SIZE - 1) {
                    __builtin_prefetch(handle.address(), 0, 3);
                }
            }
            
            // 
            if (__builtin_expect(batch.empty(), false)) [[unlikely]] {
                empty_iterations++;
                
                // spin -> yield -> sleep -> condition_variable
                if (empty_iterations < 64) {
                    // Level 1: 
                    for (int i = 0; i < 64; ++i) {
                        if (!coroutine_queue_.empty()) break;
                        __builtin_ia32_pause(); // x86 pause instruction
                    }
                } else if (empty_iterations < 256) {
                    // Level 2: yield to other threads
                    std::this_thread::yield();
                } else if (empty_iterations < 1024) {
                    // Level 3: 
                    std::this_thread::sleep_for(wait_duration);
                    wait_duration = std::min(wait_duration * 2, std::chrono::duration_cast<std::chrono::nanoseconds>(max_wait));
                } else {
                    // Level 4: CPU
                    std::unique_lock<std::mutex> lock(cv_mutex_);
                    cv_.wait_for(lock, max_wait, [this] { 
                        return stop_flag_.load(std::memory_order_relaxed) || queue_size_.load(std::memory_order_relaxed) > 0; 
                    });
                }
                continue;
            }
            
            // 
            empty_iterations = 0;
            wait_duration = std::chrono::microseconds(10);
            
            //  - 
            for (auto handle : batch) {
                if (handle && !handle.done() && !stop_flag_.load()) {
                    // 
                    void* addr = handle.address();
                    if (!addr) {
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                        continue;
                    }
                    
                    // 
                    uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);
                    if (addr_val < 0x1000 || addr_val == 0xffffffffffffffff) {
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                        continue;
                    }
                    
                    try {
                        handle.resume();
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        
                        // 
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    } catch (const std::exception& e) {
                        // 
                        std::cerr << " ( " << scheduler_id_ << "): " 
                                  << e.what() << std::endl;
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    } catch (...) {
                        // 
                        std::cerr << " ( " << scheduler_id_ << ")" << std::endl;
                        completed_coroutines_.fetch_add(1, std::memory_order_relaxed);
                        load_balancer_ptr->on_task_completed(scheduler_id_);
                    }
                }
            }
            
            // 
            load_balancer_ptr->update_load(scheduler_id_, queue_size_.load());
        }
    }

public:
    CoroutineScheduler(size_t id) 
        : scheduler_id_(id), start_time_(std::chrono::steady_clock::now()) {
        // 
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
            // worker
            cv_.notify_all();
            
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            
            //  - 
            std::coroutine_handle<> handle;
            while (coroutine_queue_.dequeue(handle)) {
                if (handle && !handle.done()) {
                    try {
                        handle.destroy();
                    } catch (...) {
                        // 
                    }
                }
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }
    
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;
        
        // 
        void* addr = handle.address();
        if (!addr) return;
        
        // 
        uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);
        if (addr_val < 0x1000 || addr_val == 0xffffffffffffffff) {
            return;
        }
        
        total_coroutines_.fetch_add(1, std::memory_order_relaxed);
        
        // 
        coroutine_queue_.enqueue(handle);
        
        // 
        queue_size_.fetch_add(1, std::memory_order_relaxed);
        
        // worker
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
        }
        cv_.notify_one();
        
        // 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.update_load(scheduler_id_, queue_size_.load());
    }
    
    size_t get_queue_size() const {
        return queue_size_.load(std::memory_order_relaxed);
    }
    
    // 
    size_t try_steal_work(std::vector<std::coroutine_handle<>>& stolen_tasks, size_t max_steal = 32) {
        if (queue_size_.load(std::memory_order_relaxed) <= 1) {
            return 0; // 
        }
        
        size_t stolen_count = 0;
        std::coroutine_handle<> handle;
        
        // max_steal
        size_t target_steal = std::min(queue_size_.load() / 2, max_steal);
        
        while (stolen_count < target_steal && coroutine_queue_.dequeue(handle)) {
            if (handle && !handle.done()) {
                stolen_tasks.push_back(handle);
                stolen_count++;
                queue_size_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        
        return stolen_count;
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
//  - 12
// ==========================================

class CoroutinePool {
private:
    static std::atomic<CoroutinePool*> instance_;
    
    // CPU
    const size_t NUM_SCHEDULERS;
    
    // 
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    //  - CPU
    std::unique_ptr<lockfree::ThreadPool> thread_pool_;
    
    std::atomic<bool> stop_flag_{false};
    
    // 
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    CoroutinePool()
        : NUM_SCHEDULERS(1), // 1 - 
          start_time_(std::chrono::steady_clock::now()) {
        // CPU
        schedulers_.reserve(NUM_SCHEDULERS);
        for (size_t i = 0; i < NUM_SCHEDULERS; ++i) {
            schedulers_.emplace_back(std::make_unique<CoroutineScheduler>(i));
        }
        
        // 
        size_t thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 8; // 

        // /
        thread_count = std::max(thread_count, static_cast<size_t>(8)); // 8
        thread_count = std::min(thread_count, static_cast<size_t>(24)); // 24

        thread_pool_ = std::make_unique<lockfree::ThreadPool>(thread_count);

        // 
        static bool first_init = true;
        if (first_init) {
            std::cout << "FlowCoro - " << NUM_SCHEDULERS 
                      << " + " << thread_count 
                      << " ()" << std::endl;
            first_init = false;
        }

        // 
        for (auto& scheduler : schedulers_) {
            scheduler->start();
        }
        
        // 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        load_balancer.set_scheduler_count(NUM_SCHEDULERS);
    }

    ~CoroutinePool() {
        stop_flag_.store(true);
        
        // 
        for (auto& scheduler : schedulers_) {
            if (scheduler) {
                scheduler->stop();
            }
        }
        
        schedulers_.clear();
        std::cout << "FlowCoro (" << NUM_SCHEDULERS << ")" << std::endl;
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

    //  - 
    void schedule_coroutine(std::coroutine_handle<> handle) {
        if (!handle || handle.done() || stop_flag_.load()) return;

        // 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        
        size_t scheduler_index = load_balancer.select_scheduler();
        
        // 
        schedulers_[scheduler_index]->schedule_coroutine(handle);
    }

    // CPU -  ()
    void schedule_task(std::function<void()> task) {
        if (stop_flag_.load()) return;

        total_tasks_.fetch_add(1, std::memory_order_relaxed);

        //  - 
        thread_pool_->enqueue([this, task = std::move(task)]() {
            try {
                task();
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                std::cerr << ": " << e.what() << std::endl;
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                std::cerr << "" << std::endl;
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    //  - 
    void drive() {
        if (stop_flag_.load(std::memory_order_relaxed)) return;
        
        // 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        
        // 1.  - 
        manager.process_timer_queue();
        
        // 2.  - 
        manager.process_ready_queue();
        
        // 3. 
        // 
        for (auto& scheduler : schedulers_) {
            if (scheduler) {
                std::this_thread::yield();
            }
        }
        
        // 4.  - 
        manager.process_pending_tasks();
    }

    // 
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
        
        // 
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
            std::max(std::thread::hardware_concurrency(), static_cast<unsigned int>(32)), // thread_pool_workers ()
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

        std::cout << "\n=== FlowCoro  ===" << std::endl;
        std::cout << " : " << stats.uptime.count() << " ms" << std::endl;
        std::cout << " : " << stats.num_schedulers << " + " << std::endl;
        std::cout << " : " << stats.thread_pool_workers << " " << std::endl;
        std::cout << " : " << stats.pending_coroutines << std::endl;
        std::cout << " : " << stats.total_coroutines << std::endl;
        std::cout << " : " << stats.completed_coroutines << std::endl;
        std::cout << " : " << stats.total_tasks << std::endl;
        std::cout << " : " << stats.completed_tasks << std::endl;
        std::cout << " : " << std::fixed << std::setprecision(1)
                  << (stats.coroutine_completion_rate * 100) << "%" << std::endl;
        std::cout << " : " << std::fixed << std::setprecision(1)
                  << (stats.task_completion_rate * 100) << "%" << std::endl;
        
        // 
        std::cout << "\n---  ---" << std::endl;
        for (const auto& scheduler : schedulers_) {
            if (scheduler) {
                std::cout << " #" << scheduler->get_scheduler_id() 
                          << " - : " << scheduler->get_queue_size()
                          << ", : " << scheduler->get_total_coroutines()
                          << ", : " << scheduler->get_completed_coroutines()
                          << ", : " << scheduler->get_uptime().count() << "ms" << std::endl;
            }
        }
        
        // 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        auto& load_balancer = manager.get_load_balancer();
        auto load_stats = load_balancer.get_load_stats();
        
        std::cout << "\n---  ---" << std::endl;
        for (const auto& stat : load_stats) {
            std::cout << " #" << stat.scheduler_id 
                      << " - : " << stat.queue_load
                      << ", : " << stat.total_processed
                      << ", : " << std::fixed << std::setprecision(2) << stat.load_score
                      << std::endl;
        }
        
        std::cout << "===============================" << std::endl;
    }
};

// 
std::atomic<CoroutinePool*> CoroutinePool::instance_{nullptr};

// ==========================================
//  - 
// ==========================================

// 
void schedule_coroutine_enhanced(std::coroutine_handle<> handle) {
    CoroutinePool::get_instance().schedule_coroutine(handle);
}

//  ()
void schedule_task_enhanced(std::function<void()> task) {
    CoroutinePool::get_instance().schedule_task(std::move(task));
}

//  - 
void drive_coroutine_pool() {
    CoroutinePool::get_instance().drive();
}

// 
void print_pool_stats() {
    CoroutinePool::get_instance().print_stats();
}

// 
void shutdown_coroutine_pool() {
    CoroutinePool::shutdown();
}

//  - CPU
void run_until_complete(Task<void>& task) {
    std::atomic<bool> completed{false};

    // 
    auto& manager = CoroutineManager::get_instance();

    //  - 
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            std::cerr << ": " << e.what() << std::endl;
            completed.store(true, std::memory_order_release);
        } catch (...) {
            std::cerr << "" << std::endl;
            completed.store(true, std::memory_order_release);
        }
    }();

    // 
    manager.schedule_resume(completion_task.handle);

    // CPU
    auto wait_duration = std::chrono::microseconds(1);
    constexpr auto max_wait = std::chrono::milliseconds(1);
    size_t iterations = 0;
    
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 
        
        iterations++;
        
        // 
        if (iterations < 100) {
            // 100
            std::this_thread::yield();
        } else if (iterations < 1000) {
            // 
            std::this_thread::sleep_for(wait_duration);
            wait_duration = std::min(wait_duration * 2, std::chrono::microseconds(100));
        } else {
            // CPU
            std::this_thread::sleep_for(max_wait);
        }
    }

    // 
    manager.drive();
}

template<typename T>
void run_until_complete(Task<T>& task) {
    std::atomic<bool> completed{false};

    // 
    auto& manager = CoroutineManager::get_instance();

    //  - 
    auto completion_task = [&]() -> Task<void> {
        try {
            co_await task;
            completed.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            std::cerr << ": " << e.what() << std::endl;
            completed.store(true, std::memory_order_release);
        } catch (...) {
            std::cerr << "" << std::endl;
            completed.store(true, std::memory_order_release);
        }
    }();

    // 
    manager.schedule_resume(completion_task.handle);

    // CPU
    auto wait_duration = std::chrono::microseconds(1);
    constexpr auto max_wait = std::chrono::milliseconds(1);
    size_t iterations = 0;
    
    while (!completed.load(std::memory_order_acquire)) {
        manager.drive(); // 
        
        iterations++;
        
        // 
        if (iterations < 100) {
            // 100
            std::this_thread::yield();
        } else if (iterations < 1000) {
            // 
            std::this_thread::sleep_for(wait_duration);
            wait_duration = std::min(wait_duration * 2, std::chrono::microseconds(100));
        } else {
            // CPU
            std::this_thread::sleep_for(max_wait);
        }
    }

    // 
    manager.drive();
}

// 
template void run_until_complete<int>(Task<int>&);
template void run_until_complete<void>(Task<void>&);

} // namespace flowcoro
