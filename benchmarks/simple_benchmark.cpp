#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>

using namespace flowcoro;

// 性能计时器
class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    
public:
    Timer() : start_time(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ms() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
    
    void reset() {
        start_time = std::chrono::high_resolution_clock::now();
    }
};

// 基准测试结果
struct BenchmarkResult {
    std::string name;
    int iterations;
    double total_time_ms;
    double avg_time_ms;
    double ops_per_second;
    
    void print() const {
        std::cout << std::left << std::setw(25) << name 
                  << " | " << std::setw(10) << iterations
                  << " | " << std::setw(12) << std::fixed << std::setprecision(3) << total_time_ms << "ms"
                  << " | " << std::setw(12) << std::fixed << std::setprecision(6) << avg_time_ms << "ms"
                  << " | " << std::setw(15) << std::fixed << std::setprecision(2) << ops_per_second << " ops/sec"
                  << std::endl;
    }
};

// 1. 基本协程创建和执行测试
Task<int> simple_coroutine(int value) {
    co_return value * 2;
}

BenchmarkResult benchmark_basic_coroutine(int iterations) {
    Timer timer;
    
    for (int i = 0; i < iterations; ++i) {
        auto result = sync_wait([&]() {
            return simple_coroutine(i);
        });
        (void)result; // 避免未使用变量警告
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Basic Coroutine",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

// 2. 协程调度测试
std::atomic<int> schedule_counter{0};

Task<void> scheduled_coroutine() {
    schedule_counter.fetch_add(1);
    co_return;
}

// 7. 无锁队列性能测试
BenchmarkResult benchmark_lockfree_queue(int iterations) {
    Timer timer;
    lockfree::Queue<int> queue;
    
    // 入队测试
    for (int i = 0; i < iterations; ++i) {
        queue.enqueue(i);
    }
    
    // 出队测试
    int value;
    int dequeue_count = 0;
    while (queue.dequeue(value) && dequeue_count < iterations) {
        dequeue_count++;
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Lockfree Queue Ops",
        iterations * 2,  // 入队 + 出队
        total_time,
        total_time / (iterations * 2),
        (iterations * 2) * 1000.0 / total_time
    };
}

// 8. 协程池调度性能测试
std::atomic<int> pool_counter{0};

Task<void> pool_test_task(int id) {
    pool_counter.fetch_add(1);
    // 模拟一些轻量级工作
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    co_return;
}

BenchmarkResult benchmark_coroutine_pool(int iterations) {
    Timer timer;
    pool_counter = 0;
    
    // 启用协程池功能
    enable_v2_features();
    
    std::vector<Task<void>> tasks;
    tasks.reserve(iterations);
    
    // 创建任务（会立即投递到协程池）
    for (int i = 0; i < iterations; ++i) {
        tasks.emplace_back(pool_test_task(i));
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        sync_wait([&task]() -> Task<void> {
            co_await task;
            co_return;
        });
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Coroutine Pool",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

BenchmarkResult benchmark_coroutine_scheduling(int iterations) {
    Timer timer;
    schedule_counter = 0;
    
    for (int i = 0; i < iterations; ++i) {
        sync_wait([]() {
            return scheduled_coroutine();
        });
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Coroutine Scheduling",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

// 3. sleep_for性能测试
Task<void> sleep_task(std::chrono::milliseconds duration) {
    co_await sleep_for(duration);
}

BenchmarkResult benchmark_sleep_for(int iterations) {
    Timer timer;
    
    for (int i = 0; i < iterations; ++i) {
        sync_wait([]() {
            return sleep_task(std::chrono::milliseconds(1));
        });
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Sleep For (1ms)",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

// 4. when_any性能测试
Task<int> compute_task(int value, int computation_size) {
    // 使用CPU计算而不是sleep_for
    volatile int sum = 0;
    for (int i = 0; i < computation_size; ++i) {
        sum += i;
    }
    co_return value;
}

BenchmarkResult benchmark_when_any(int iterations) {
    Timer timer;
    
    for (int i = 0; i < iterations; ++i) {
        auto test_coro = []() -> Task<int> {
            auto task1 = compute_task(1, 50000);    // 最少计算量
            auto task2 = compute_task(2, 100000);   // 中等计算量
            auto task3 = compute_task(3, 150000);   // 较多计算量
            
            auto result = co_await when_any(std::move(task1), std::move(task2), std::move(task3));
            co_return result.first;
        };
        
        auto result = sync_wait(std::move(test_coro));
        (void)result;
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "When Any (3 tasks)",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

// 5. 内存池性能测试（如果可用）
BenchmarkResult benchmark_memory_operations(int iterations) {
    Timer timer;
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);
    
    // 分配
    for (int i = 0; i < iterations; ++i) {
        ptrs.push_back(std::malloc(1024));
    }
    
    // 释放
    for (auto ptr : ptrs) {
        std::free(ptr);
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Memory Alloc/Free (1KB)",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

// 6. 并发协程测试
std::atomic<int> concurrent_counter{0};

Task<void> concurrent_task(int id) {
    concurrent_counter.fetch_add(1);
    co_await sleep_for(std::chrono::milliseconds(1));
    concurrent_counter.fetch_add(1);
}

BenchmarkResult benchmark_concurrent_coroutines(int iterations) {
    Timer timer;
    concurrent_counter = 0;
    
    for (int i = 0; i < iterations; ++i) {
        sync_wait([&]() {
            return concurrent_task(i);
        });
    }
    
    double total_time = timer.elapsed_ms();
    
    return {
        "Concurrent Coroutines",
        iterations,
        total_time,
        total_time / iterations,
        iterations * 1000.0 / total_time
    };
}

void print_header() {
    std::cout << "\n=== FlowCoro 性能基准测试 ===" << std::endl;
    std::cout << std::string(90, '=') << std::endl;
    std::cout << std::left << std::setw(25) << "测试名称"
              << " | " << std::setw(10) << "迭代次数"
              << " | " << std::setw(12) << "总时间"
              << " | " << std::setw(12) << "平均时间"
              << " | " << std::setw(15) << "吞吐量"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;
}

void print_footer() {
    std::cout << std::string(90, '=') << std::endl;
}

int main() {
    try {
        print_header();
        
        // 运行基准测试
        const int iterations = 1000;
        const int small_iterations = 100;
        
        // 基本协程测试
        auto result1 = benchmark_basic_coroutine(iterations * 10);
        result1.print();
        
        // 协程调度测试
        auto result2 = benchmark_coroutine_scheduling(iterations);
        result2.print();
        
        // 内存操作测试
        auto result3 = benchmark_memory_operations(iterations);
        result3.print();
        
        // 并发协程测试
        auto result4 = benchmark_concurrent_coroutines(small_iterations);
        result4.print();
        
        // sleep_for测试
        auto result5 = benchmark_sleep_for(small_iterations / 10);
        result5.print();
        
        // when_any测试
        auto result6 = benchmark_when_any(small_iterations / 10);
        result6.print();
        
        // 无锁队列测试
        auto result7 = benchmark_lockfree_queue(iterations);
        result7.print();
        
        // 协程池测试
        auto result8 = benchmark_coroutine_pool(small_iterations);
        result8.print();
        
        print_footer();
        
        std::cout << "\n基准测试完成！" << std::endl;
        std::cout << "注意：实际性能可能因系统负载和硬件配置而异。" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "基准测试错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
