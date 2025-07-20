#include <iostream>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>

#include "../include/flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// 性能测试辅助类
class PerformanceTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    PerformanceTimer() : start_time_(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ms() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
        return duration.count() / 1000000.0; // 转换为毫秒
    }
    
    void print_result(const std::string& test_name, int operations) const {
        double elapsed = elapsed_ms();
        double ops_per_second = operations / (elapsed / 1000.0);
        std::cout << test_name << ": " << elapsed << "ms, " 
                 << static_cast<long>(ops_per_second) << " ops/sec" << std::endl;
    }
};

// 测试协程创建和销毁性能
TEST_CASE(coroutine_creation_performance) {
    const int num_coroutines = 10000;
    
    PerformanceTimer timer;
    
    std::atomic<int> counter{0};
    std::vector<Task<void>> tasks;
    
    for (int i = 0; i < num_coroutines; ++i) {
        tasks.push_back([&counter]() -> Task<void> {
            counter.fetch_add(1);
            co_return;
        }());
    }
    
    // 等待所有协程完成
    for (auto& task : tasks) {
        sync_wait(std::move(task));
    }
    
    timer.print_result("Coroutine Creation", num_coroutines);
    TEST_EXPECT_EQ(counter.load(), num_coroutines);
}

// 测试线程池性能
TEST_CASE(thread_pool_performance) {
    const int num_tasks = 50000;
    const int num_threads = 8;
    
    ThreadPool pool(num_threads);
    PerformanceTimer timer;
    
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([&completed]() {
            // 模拟一些轻量级工作
            volatile int x = 0;
            for (int j = 0; j < 100; ++j) {
                x += j;
            }
            completed.fetch_add(1);
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    timer.print_result("Thread Pool Tasks", num_tasks);
    TEST_EXPECT_EQ(completed.load(), num_tasks);
}

// 测试内存池性能
TEST_CASE(memory_pool_performance) {
    const int num_allocations = 100000;
    const size_t block_size = 64;
    const size_t pool_size = 1000;
    
    MemoryPool pool(block_size, pool_size);
    PerformanceTimer timer;
    
    std::vector<void*> ptrs;
    ptrs.reserve(pool_size);
    
    // 分配-释放循环
    for (int i = 0; i < num_allocations; ++i) {
        if (ptrs.size() < pool_size) {
            void* ptr = pool.allocate();
            if (ptr) ptrs.push_back(ptr);
        }
        
        if (ptrs.size() > pool_size / 2 && !ptrs.empty()) {
            pool.deallocate(ptrs.back());
            ptrs.pop_back();
        }
    }
    
    // 清理剩余内存
    for (void* ptr : ptrs) {
        pool.deallocate(ptr);
    }
    
    timer.print_result("Memory Pool Operations", num_allocations);
    TEST_EXPECT_TRUE(true); // 如果执行到这里说明没有崩溃
}

// 测试无锁队列性能
TEST_CASE(lockfree_queue_performance) {
    const int num_operations = 1000000;
    LockFreeQueue<int> queue;
    
    PerformanceTimer timer;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    // 生产者线程
    std::thread producer([&queue, &produced, num_operations]() {
        for (int i = 0; i < num_operations; ++i) {
            queue.push(i);
            produced.fetch_add(1);
        }
    });
    
    // 消费者线程
    std::thread consumer([&queue, &consumed, num_operations]() {
        int value;
        while (consumed.load() < num_operations) {
            if (queue.try_pop(value)) {
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    timer.print_result("LockFree Queue Operations", num_operations * 2);
    TEST_EXPECT_EQ(produced.load(), num_operations);
    TEST_EXPECT_EQ(consumed.load(), num_operations);
}

// 测试协程与线程池结合的性能
TEST_CASE(coroutine_threadpool_performance) {
    const int num_tasks = 10000;
    ThreadPool pool(4);
    
    PerformanceTimer timer;
    
    std::atomic<int> completed{0};
    std::vector<Task<int>> tasks;
    
    for (int i = 0; i < num_tasks; ++i) {
        tasks.push_back([&pool, &completed, i]() -> Task<int> {
            int result = co_await pool.schedule([i]() {
                // 模拟一些计算
                int sum = 0;
                for (int j = 0; j <= i % 100; ++j) {
                    sum += j;
                }
                return sum;
            });
            completed.fetch_add(1);
            co_return result;
        }());
    }
    
    // 等待所有任务完成
    int total_result = 0;
    for (auto& task : tasks) {
        total_result += sync_wait(std::move(task));
    }
    
    timer.print_result("Coroutine+ThreadPool", num_tasks);
    TEST_EXPECT_EQ(completed.load(), num_tasks);
    TEST_EXPECT_TRUE(total_result > 0);
}

// 测试大量并发协程性能
TEST_CASE(massive_coroutine_concurrency) {
    const int num_coroutines = 50000;
    
    PerformanceTimer timer;
    
    std::atomic<int> counter{0};
    
    auto create_coroutine_batch = [&counter](int start, int count) -> Task<void> {
        std::vector<Task<void>> batch;
        
        for (int i = start; i < start + count; ++i) {
            batch.push_back([&counter, i]() -> Task<void> {
                // 模拟一些异步工作
                counter.fetch_add(1);
                co_return;
            }());
        }
        
        // 等待这一批完成
        for (auto& task : batch) {
            co_await std::move(task);
        }
    };
    
    const int batch_size = 1000;
    std::vector<Task<void>> batch_tasks;
    
    for (int i = 0; i < num_coroutines; i += batch_size) {
        batch_tasks.push_back(create_coroutine_batch(i, 
            std::min(batch_size, num_coroutines - i)));
    }
    
    for (auto& batch_task : batch_tasks) {
        sync_wait(std::move(batch_task));
    }
    
    timer.print_result("Massive Coroutine Concurrency", num_coroutines);
    TEST_EXPECT_EQ(counter.load(), num_coroutines);
}

// 测试内存使用效率
TEST_CASE(memory_efficiency) {
    const int num_objects = 10000;
    
    struct TestObject {
        int data[16]; // 64 bytes
        TestObject() { std::fill(std::begin(data), std::end(data), 42); }
    };
    
    // 使用对象池
    {
        PerformanceTimer timer;
        ObjectPool<TestObject> pool(1000);
        
        std::vector<std::unique_ptr<TestObject, std::function<void(TestObject*)>>> objects;
        
        for (int i = 0; i < num_objects; ++i) {
            auto obj = pool.acquire();
            if (obj) {
                objects.push_back(std::move(obj));
            }
            
            // 定期释放一些对象
            if (i % 100 == 99) {
                for (int j = 0; j < 50 && !objects.empty(); ++j) {
                    objects.pop_back();
                }
            }
        }
        
        timer.print_result("Object Pool", num_objects);
    }
    
    TEST_EXPECT_TRUE(true);
}

int main() {
    TEST_SUITE("FlowCoro Performance Tests");
    
    std::cout << "\n🚀 Performance Test Results:" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
