#pragma once
#include "lockfree.h"
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <future>
#include <memory>

namespace lockfree {

// 无锁线程池实现
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
        // 设置析构标志，避免新任务入队
        stop_.store(true, std::memory_order_release);

        // 给工作线程更多时间完成当前任务
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(500); // 增加超时时间

        // 等待活跃线程数降为0或超时
        while (active_threads_.load(std::memory_order_acquire) > 0) {
            auto current_time = std::chrono::steady_clock::now();
            if (current_time - start_time > timeout) {
                break; // 超时，停止等待
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 增加sleep时间
        }

        // 尝试 join 所有线程
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                // 给每个线程一个更长的join机会
                try {
                    worker.join();
                } catch (...) {
                    // 如果join失败，使用detach
                    try {
                        worker.detach();
                    } catch (...) {
                        // 忽略detach异常
                    }
                }
            }
        }

        workers_.clear();

        // 清空任务队列
        std::function<void()> unused_task;
        while (task_queue_.dequeue(unused_task)) {
            // 清空队列，避免析构时访问已失效的对象
        }
    }

    // 提交任务并返回future
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        auto task = std::make_shared<std::packaged_task<return_type()>>(
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

    // 提交简单的void任务
    void enqueue_void(std::function<void()> task) {
        if (!stop_.load(std::memory_order_acquire)) {
            task_queue_.enqueue(std::move(task));
        } else {
            throw std::runtime_error("ThreadPool is stopped, cannot enqueue tasks");
        }
    }

    void shutdown() {
        // 设置 stop 标志
        stop_.store(true, std::memory_order_release);

        // 等待所有工作线程完成当前任务并退出
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();

        // 清理队列中剩余的任务
        std::function<void()> unused_task;
        while (task_queue_.dequeue(unused_task)) {
            // 清空队列，避免析构时访问已失效的对象
        }
    }

    size_t active_thread_count() const {
        return active_threads_.load(std::memory_order_acquire);
    }

    bool is_stopped() const {
        return stop_.load(std::memory_order_acquire);
    }

private:
    void worker_loop() {
        std::function<void()> task;

        while (!stop_.load(std::memory_order_acquire)) {
            if (task_queue_.dequeue(task)) {
                try {
                    task();
                } catch (const std::exception& e) {
                    // 异常处理，但不中断工作线程
                }
            } else {
                // 没有任务时短暂休眠，避免忙等待
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // 线程退出前减少计数
        active_threads_.fetch_sub(1, std::memory_order_acq_rel);

        // 线程退出前再次尝试处理剩余任务
        while (task_queue_.dequeue(task)) {
            try {
                task();
            } catch (...) {
                // 忽略异常
            }
        }
    }
};

// 工作窃取线程池实现
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

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        if (!stop_.load(std::memory_order_acquire)) {
            auto wrapper = [task]() { (*task)(); };

            // 如果当前线程是工作线程，优先放入本地队列
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

            // 1. 先检查本地队列
            if (worker_data_[worker_index]->local_queue.dequeue(task)) {
                found_work = true;
                if (worker_data_[worker_index]->local_queue.empty()) {
                    worker_data_[worker_index]->has_work.store(false, std::memory_order_release);
                }
            }
            // 2. 检查全局队列
            else if (global_queue_.dequeue(task)) {
                found_work = true;
            }
            // 3. 尝试从其他工作线程偷取任务
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
                    // 异常处理
                }
            } else {
                std::this_thread::yield();
            }
        }

        active_workers_.fetch_sub(1, std::memory_order_acq_rel);
    }
};

// 定义thread_local变量
} // namespace lockfree
