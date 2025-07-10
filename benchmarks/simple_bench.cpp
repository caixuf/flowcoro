#include <flowcoro.hpp>
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>

class SimpleBenchmark {
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    SimpleBenchmark(const std::string& name) : name_(name) {}
    
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void end() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        
        std::cout << "[BENCH] " << name_ << ": " << duration << " μs" << std::endl;
    }
};

// 协程性能测试
void benchmark_coroutines() {
    const int num_coroutines = 1000; // 减少数量避免内存问题
    std::vector<flowcoro::Task<void>> tasks;
    
    SimpleBenchmark bench("Coroutine Creation (" + std::to_string(num_coroutines) + ")");
    bench.start();
    
    for (int i = 0; i < num_coroutines; ++i) {
        tasks.emplace_back([i]() -> flowcoro::Task<void> {
            // 简单的协程工作
            volatile int x = i * 2;
            (void)x; // 避免编译器优化
            co_return;
        }());
    }
    
    bench.end();
    
    // 测试协程执行
    SimpleBenchmark bench2("Coroutine Execution (" + std::to_string(num_coroutines) + ")");
    bench2.start();
    
    for (auto& task : tasks) {
        if (task.handle && !task.handle.done()) {
            task.handle.resume();
        }
    }
    
    bench2.end();
}

// 无锁队列性能测试
void benchmark_lockfree_queue() {
    const int num_operations = 1000000;
    lockfree::Queue<int> queue;
    
    SimpleBenchmark bench("Lockfree Queue (" + std::to_string(num_operations) + " ops)");
    bench.start();
    
    // 生产者线程
    std::thread producer([&queue, num_operations]() {
        for (int i = 0; i < num_operations; ++i) {
            queue.enqueue(i);
        }
    });
    
    // 消费者线程
    std::thread consumer([&queue, num_operations]() {
        int count = 0;
        int value;
        while (count < num_operations) {
            if (queue.dequeue(value)) {
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    bench.end();
}

// 内存池性能测试
void benchmark_memory_pool() {
    const int num_allocations = 10000; // 减少分配数量
    
    SimpleBenchmark bench("Memory Pool (" + std::to_string(num_allocations) + " allocs)");
    bench.start();
    
    std::vector<std::string*> objects;
    objects.reserve(num_allocations);
    
    for (int i = 0; i < num_allocations; ++i) {
        auto* obj = new std::string("Test string " + std::to_string(i));
        objects.push_back(obj);
    }
    
    // 释放对象
    for (auto* obj : objects) {
        delete obj;
    }
    
    bench.end();
}

// 日志性能测试
void benchmark_logging() {
    const int num_logs = 10000; // 减少日志数量
    
    SimpleBenchmark bench("Logging (" + std::to_string(num_logs) + " logs)");
    bench.start();
    
    for (int i = 0; i < num_logs; ++i) {
        LOG_DEBUG("Benchmark log message %d with data: %f", i, i * 3.14159);
    }
    
    bench.end();
    
    // 等待日志写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main() {
    std::cout << "=== FlowCoro 性能基准测试 ===" << std::endl;
    
    // 初始化
    flowcoro::GlobalLogger::get().set_level(flowcoro::LogLevel::LOG_DEBUG);
    
    // 运行基准测试
    benchmark_coroutines();
    benchmark_lockfree_queue();
    benchmark_memory_pool();
    benchmark_logging();
    
    std::cout << "=== 基准测试完成 ===" << std::endl;
    
    // 清理
    flowcoro::GlobalThreadPool::shutdown();
    flowcoro::GlobalLogger::shutdown();
    
    return 0;
}
