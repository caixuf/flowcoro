#include <flowcoro.hpp>
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <string>

using namespace flowcoro;

// 用于处理vector<Task>的when_all辅助函数
template<typename T>
Task<std::vector<T>> when_all_vector(std::vector<Task<T>>&& tasks) {
    std::vector<T> results;
    results.reserve(tasks.size());
    
    // 顺序执行每个任务
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
    
    co_return results;
}

class AccurateBenchmark {
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    size_t operations_;
    
public:
    AccurateBenchmark(const std::string& name, size_t operations = 1) 
        : name_(name), operations_(operations) {}
    
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void end() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time_).count();
        
        double ops_per_sec = operations_ * 1000000.0 / duration_us;
        double ns_per_op = static_cast<double>(duration_ns) / operations_;
        
        std::cout << "[BENCH] " << name_ << ":\n";
        std::cout << "  Total time: " << duration_us << " μs\n";
        std::cout << "  Operations: " << operations_ << "\n";
        std::cout << "  Throughput: " << static_cast<size_t>(ops_per_sec) << " ops/sec\n";
        std::cout << "  Latency: " << ns_per_op << " ns/op\n";
        std::cout << std::endl;
    }
};

// 测试真正的协程异步操作
Task<int> async_compute_task(int value, int delay_ms = 1) {
    // 使用真正的协程友好sleep
    co_await sleep_for(std::chrono::milliseconds(delay_ms));
    co_return value * 2;
}

// 测试协程创建和执行的真实性能
void benchmark_real_coroutines() {
    const int num_tasks = 100;  // 减少数量以测试真实异步操作
    
    {
        AccurateBenchmark bench("Real Coroutine Tasks (creation + execution)", num_tasks);
        bench.start();
        
        std::vector<Task<int>> tasks;
        tasks.reserve(num_tasks);
        
        // 创建协程任务
        for (int i = 0; i < num_tasks; ++i) {
            tasks.emplace_back(async_compute_task(i, 1)); // 1ms延迟
        }
        
        // 使用when_all_vector等待所有任务完成
        auto results = sync_wait(when_all_vector(std::move(tasks)));
        
        bench.end();
        
        // 验证结果
        std::cout << "  Results (first 5): ";
        for (size_t i = 0; i < std::min(results.size(), size_t(5)); ++i) {
            std::cout << results[i] << " ";
        }
        std::cout << "\n" << std::endl;
    }
}

// 测试协程创建开销
void benchmark_coroutine_creation() {
    const int num_coroutines = 10000;
    
    AccurateBenchmark bench("Coroutine Creation Only", num_coroutines);
    bench.start();
    
    std::vector<Task<int>> tasks;
    tasks.reserve(num_coroutines);
    
    for (int i = 0; i < num_coroutines; ++i) {
        tasks.emplace_back([i]() -> Task<int> {
            co_return i * 2;
        }());
    }
    
    bench.end();
}

// 测试sleep_for性能
void benchmark_sleep_for() {
    const int num_sleeps = 50;
    
    AccurateBenchmark bench("sleep_for Performance", num_sleeps);
    bench.start();
    
    auto task = [num_sleeps]() -> Task<void> {
        for (int i = 0; i < num_sleeps; ++i) {
            co_await sleep_for(std::chrono::milliseconds(1));
        }
    }();
    
    sync_wait(std::move(task));
    
    bench.end();
}

// 测试when_all在不同规模下的性能
void benchmark_when_all_scaling() {
    std::vector<int> scales = {10, 100, 500, 1000};
    
    for (int scale : scales) {
        AccurateBenchmark bench("when_all scaling (" + std::to_string(scale) + " tasks)", scale);
        bench.start();
        
        std::vector<Task<int>> tasks;
        tasks.reserve(scale);
        
        for (int i = 0; i < scale; ++i) {
            tasks.emplace_back([i]() -> Task<int> {
                // 无延迟的快速协程
                co_return i;
            }());
        }
        
        auto results = sync_wait(when_all_vector(std::move(tasks)));
        
        bench.end();
    }
}

// 测试修复后的内存池性能和动态扩展
void benchmark_memory_pool() {
    std::cout << "[MEMORY] Testing dynamic expandable memory pool...\n";
    
    const int num_allocations = 20000;  // 增加测试数量
    const size_t block_size = 64;
    
    // 测试标准分配器
    {
        AccurateBenchmark bench("Standard Allocator", num_allocations * 2);
        bench.start();
        
        std::vector<void*> ptrs;
        ptrs.reserve(num_allocations);
        
        // 分配
        for (int i = 0; i < num_allocations; ++i) {
            ptrs.push_back(malloc(block_size));
        }
        
        // 释放
        for (void* ptr : ptrs) {
            free(ptr);
        }
        
        bench.end();
    }
    
    // 测试动态扩展内存池
    {
        AccurateBenchmark bench("Dynamic Memory Pool", num_allocations * 2);
        bench.start();
        
        // 创建较小的初始内存池，测试动态扩展
        MemoryPool pool(block_size, 100);  // 只有100个初始块
        std::vector<void*> ptrs;
        ptrs.reserve(num_allocations);
        
        // 显示初始状态
        auto initial_stats = pool.get_stats();
        std::cout << "  Initial pool: " << initial_stats.total_blocks 
                  << " blocks in " << initial_stats.memory_chunks << " chunks\n";
        
        // 分配超出初始容量的内存
        for (int i = 0; i < num_allocations; ++i) {
            try {
                void* ptr = pool.allocate();
                ptrs.push_back(ptr);
                
                // 每5000次分配显示一次扩展状态
                if (i % 5000 == 0 && i > 0) {
                    auto stats = pool.get_stats();
                    std::cout << "  At " << i << " allocations: " 
                              << stats.total_blocks << " blocks in " 
                              << stats.memory_chunks << " chunks\n";
                }
            } catch (const std::exception& e) {
                std::cout << "  Allocation failed at " << i << ": " << e.what() << std::endl;
                break;
            }
        }
        
        // 释放
        for (void* ptr : ptrs) {
            pool.deallocate(ptr);
        }
        
        bench.end();
        
        // 显示最终统计
        auto final_stats = pool.get_stats();
        std::cout << "  Successfully allocated/deallocated " << ptrs.size() << " blocks\n";
        std::cout << "  Final pool state:\n";
        std::cout << "    Total blocks: " << final_stats.total_blocks << "\n";
        std::cout << "    Memory chunks: " << final_stats.memory_chunks << "\n";
        std::cout << "    Available blocks: " << final_stats.free_blocks << "\n";
        std::cout << "    Total memory: " << final_stats.total_memory_bytes / 1024 << " KB\n";
    }
    
    // 测试扩展配置
    {
        std::cout << "\n  Testing expansion configuration:\n";
        
        MemoryPool pool(block_size, 50);  // 很小的初始池
        pool.set_expansion_factor(1.5);   // 设置较小的扩展因子
        pool.set_max_total_blocks(1000);  // 设置最大限制
        
        std::vector<void*> ptrs;
        int allocated = 0;
        
        try {
            for (int i = 0; i < 2000; ++i) {  // 尝试分配超过限制的数量
                void* ptr = pool.allocate();
                ptrs.push_back(ptr);
                allocated++;
            }
        } catch (const std::exception& e) {
            std::cout << "    Reached limit at " << allocated << " allocations: " << e.what() << "\n";
        }
        
        auto stats = pool.get_stats();
        std::cout << "    Final stats: " << stats.total_blocks << " blocks, " 
                  << stats.memory_chunks << " chunks\n";
        
        // 清理
        for (void* ptr : ptrs) {
            pool.deallocate(ptr);
        }
    }
    
    std::cout << std::endl;
}

// 测试无锁队列的基本性能
void benchmark_lockfree_queue_concurrent() {
    std::cout << "[QUEUE] Testing basic lockfree queue performance...\n";
    
    const int num_operations = 10000;  // 简化测试
    lockfree::Queue<int> queue;
    
    AccurateBenchmark bench("Lockfree Queue Basic", num_operations * 2);
    bench.start();
    
    // 简单的单线程测试
    for (int i = 0; i < num_operations; ++i) {
        queue.enqueue(i);
    }
    
    int value;
    int dequeued = 0;
    while (queue.dequeue(value)) {
        dequeued++;
    }
    
    bench.end();
    
    std::cout << "  Enqueued: " << num_operations << ", Dequeued: " << dequeued << "\n" << std::endl;
}

// 内存使用测试
void benchmark_memory_usage() {
    std::cout << "[MEMORY] Testing memory efficiency...\n";
    
    // 获取初始内存
    auto get_memory_usage = []() -> size_t {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                size_t kb = std::stoull(line.substr(7));
                return kb * 1024; // 转换为字节
            }
        }
        return 0;
    };
    
    size_t initial_memory = get_memory_usage();
    
    // 创建大量协程任务
    const int num_tasks = 1000;
    std::vector<Task<int>> tasks;
    tasks.reserve(num_tasks);
    
    for (int i = 0; i < num_tasks; ++i) {
        tasks.emplace_back([i]() -> Task<int> {
            co_return i;
        }());
    }
    
    size_t after_creation = get_memory_usage();
    
    // 执行任务
    auto results = sync_wait(when_all_vector(std::move(tasks)));
    
    size_t after_execution = get_memory_usage();
    
    std::cout << "  Initial memory: " << initial_memory / 1024 << " KB\n";
    std::cout << "  After creation: " << after_creation / 1024 << " KB\n";
    std::cout << "  After execution: " << after_execution / 1024 << " KB\n";
    std::cout << "  Memory per task (creation): " << (after_creation - initial_memory) / num_tasks << " bytes\n";
    std::cout << "  Memory per task (total): " << (after_execution - initial_memory) / num_tasks << " bytes\n";
    std::cout << std::endl;
}

int main() {
    std::cout << "=== FlowCoro 精确性能基准测试 ===" << std::endl;
    std::cout << "CPU Cores: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << std::endl;
    
    try {
        // 1. 协程创建性能
        benchmark_coroutine_creation();
        
        // 2. 真实协程任务性能  
        benchmark_real_coroutines();
        
        // 3. sleep_for性能
        benchmark_sleep_for();
        
        // 4. when_all扩展性测试
        benchmark_when_all_scaling();
        
        // 5. 内存池性能对比
        benchmark_memory_pool();
        
        // 6. 无锁队列并发性能
        benchmark_lockfree_queue_concurrent();
        
        // 7. 内存使用效率
        benchmark_memory_usage();
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "=== 精确基准测试完成 ===" << std::endl;
    
    return 0;
}
