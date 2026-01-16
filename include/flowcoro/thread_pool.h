#pragma once
#include "lockfree.h"
#include "memory_pool.h"
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <future>
#include <memory>

namespace lockfree {

// 
class ThreadPool {
private:
    lockfree::Queue<std::function<void()>> task_queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_threads_{0};

public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        active_threads_.store(num_threads, std::memory_order_release);

        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                worker_loop();
            });
        }
    }

    ~ThreadPool() {
        // 
        stop_.store(true, std::memory_order_release);

        // 
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(1000); // 1

        // 0
        bool graceful_shutdown = false;
        while (active_threads_.load(std::memory_order_acquire) > 0) {
            auto current_time = std::chrono::steady_clock::now();
            if (current_time - start_time > timeout) {
                std::cout << "ThreadPool destructor timeout, forcing shutdown" << std::endl;
                break; // 
            }
            
            // 
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // 0
        if (active_threads_.load(std::memory_order_acquire) == 0) {
            graceful_shutdown = true;
            std::cout << "ThreadPool graceful shutdown completed" << std::endl;
        }

        //  join 
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                try {
                    if (graceful_shutdown) {
                        worker.join(); // join
                    } else {
                        // join
                        auto join_future = std::async(std::launch::async, [&worker]() {
                            worker.join();
                        });
                        
                        auto join_status = join_future.wait_for(std::chrono::milliseconds(100));
                        
                        if (join_status == std::future_status::timeout) {
                            std::cout << "Worker thread join timeout, detaching..." << std::endl;
                            if (worker.joinable()) {
                                worker.detach();
                            }
                        }
                    }
                } catch (...) {
                    // joindetach
                    try {
                        if (worker.joinable()) {
                            worker.detach();
                        }
                    } catch (...) {
                        // detach
                    }
                }
            }
        }

        workers_.clear();

        // 
        std::function<void()> unused_task;
        size_t remaining_tasks = 0;
        while (task_queue_.dequeue(unused_task)) {
            remaining_tasks++;
        }
        
        if (remaining_tasks > 0) {
            std::cout << "ThreadPool dropped " << remaining_tasks << " unprocessed tasks" << std::endl;
        }
    }

    // future
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        // 
        auto task = std::allocate_shared<std::packaged_task<return_type()>>(
            flowcoro::PoolAllocator<std::packaged_task<return_type()>>{},
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        if (!stop_.load(std::memory_order_acquire)) {
            task_queue_.enqueue([task]() { (*task)(); });
        } else {
            throw std::runtime_error("ThreadPool is stopped");
        }

        return result;
    }

    // void
    void enqueue_void(std::function<void()> task) {
        if (!stop_.load(std::memory_order_acquire)) {
            task_queue_.enqueue(std::move(task));
        } else {
            throw std::runtime_error("ThreadPool is stopped, cannot enqueue tasks");
        }
    }

    void shutdown() {
        //  stop 
        stop_.store(true, std::memory_order_release);

        // 
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();

        // 
        std::function<void()> unused_task;
        while (task_queue_.dequeue(unused_task)) {
            // 
        }
    }

    size_t active_thread_count() const {
        return active_threads_.load(std::memory_order_acquire);
    }

    bool is_stopped() const {
        return stop_.load(std::memory_order_acquire);
    }

    // 
    size_t estimated_queue_size() const {
        // 
        return task_queue_.size_estimate();
    }

    // 
    void print_status() const {
        std::cout << "ThreadPool Status:" << std::endl;
        std::cout << "  Active threads: " << active_thread_count() << std::endl;
        std::cout << "  Is stopped: " << (is_stopped() ? "true" : "false") << std::endl;
        std::cout << "  Estimated queue size: " << estimated_queue_size() << std::endl;
    }

private:
    void worker_loop() {
        std::function<void()> task;
        
        // CPU
        auto wait_duration = std::chrono::microseconds(100);
        constexpr auto max_wait = std::chrono::milliseconds(10);
        size_t empty_iterations = 0;

        while (!stop_.load(std::memory_order_acquire)) {
            if (task_queue_.dequeue(task)) {
                // 
                empty_iterations = 0;
                wait_duration = std::chrono::microseconds(100);
                
                try {
                    task();
                } catch (const std::exception& e) {
                    // 
                    std::cerr << "ThreadPool worker caught exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "ThreadPool worker caught unknown exception" << std::endl;
                }
            } else {
                // CPU
                empty_iterations++;
                
                if (empty_iterations < 10) {
                    // 
                    std::this_thread::yield();
                } else if (empty_iterations < 100) {
                    // 
                    std::this_thread::sleep_for(wait_duration);
                    wait_duration = std::min(wait_duration * 2, std::chrono::microseconds(1000));
                } else {
                    // 
                    std::this_thread::sleep_for(max_wait);
                }
            }
        }

        // 
        active_threads_.fetch_sub(1, std::memory_order_acq_rel);

        // 
        while (task_queue_.dequeue(task)) {
            try {
                task();
            } catch (...) {
                // 
            }
        }
    }
};

// 
class WorkStealingThreadPool {
private:
    struct alignas(64) WorkerData {
        lockfree::Queue<std::function<void()>> local_queue;
        std::atomic<bool> has_work{false};
    };

    std::vector<std::unique_ptr<WorkerData>> worker_data_;
    std::vector<std::thread> workers_;
    lockfree::Queue<std::function<void()>> global_queue_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_workers_{0};

    static thread_local size_t worker_id_;

public:
    explicit WorkStealingThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        worker_data_.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            worker_data_.emplace_back(std::make_unique<WorkerData>());
        }

        active_workers_.store(num_threads, std::memory_order_release);

        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this, i] {
                worker_id_ = i;
                worker_loop(i);
            });
        }
    }

    ~WorkStealingThreadPool() {
        shutdown();
    }

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        // 
        auto task = std::allocate_shared<std::packaged_task<return_type()>>(
            flowcoro::PoolAllocator<std::packaged_task<return_type()>>{},
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        if (!stop_.load(std::memory_order_acquire)) {
            auto wrapper = [task]() { (*task)(); };

            // 
            if (worker_id_ != SIZE_MAX && worker_id_ < worker_data_.size()) {
                worker_data_[worker_id_]->local_queue.enqueue(wrapper);
                worker_data_[worker_id_]->has_work.store(true, std::memory_order_release);
            } else {
                global_queue_.enqueue(wrapper);
            }
        } else {
            throw std::runtime_error("WorkStealingThreadPool is stopped");
        }

        return result;
    }

    void shutdown() {
        stop_.store(true, std::memory_order_release);

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();
        worker_data_.clear();
    }

    size_t active_worker_count() const {
        return active_workers_.load(std::memory_order_acquire);
    }

private:
    void worker_loop(size_t worker_index) {
        std::function<void()> task;

        while (!stop_.load(std::memory_order_acquire)) {
            bool found_work = false;

            // 1. 
            if (worker_data_[worker_index]->local_queue.dequeue(task)) {
                found_work = true;
                if (worker_data_[worker_index]->local_queue.empty()) {
                    worker_data_[worker_index]->has_work.store(false, std::memory_order_release);
                }
            }
            // 2. 
            else if (global_queue_.dequeue(task)) {
                found_work = true;
            }
            // 3. 
            else {
                for (size_t i = 0; i < worker_data_.size(); ++i) {
                    if (i != worker_index &&
                        worker_data_[i]->has_work.load(std::memory_order_acquire) &&
                        worker_data_[i]->local_queue.dequeue(task)) {
                        found_work = true;
                        break;
                    }
                }
            }

            if (found_work) {
                try {
                    task();
                } catch (const std::exception& e) {
                    // 
                }
            } else {
                std::this_thread::yield();
            }
        }

        active_workers_.fetch_sub(1, std::memory_order_acq_rel);
    }
};

// thread_local
} // namespace lockfree
