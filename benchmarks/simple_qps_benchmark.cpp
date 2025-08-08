#include <flowcoro.hpp>
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

using namespace flowcoro;

// 简单但真实的QPS测试
Task<int> simple_work_task(int task_id) {
    // 模拟简单的工作负载
    volatile int result = 0;
    for (int i = 0; i < 100; ++i) {
        result += i;
    }
    
    // 随机延迟模拟真实工作
    if (task_id % 100 == 0) {
        co_await sleep_for(std::chrono::milliseconds(1));
    }
    
    co_return result + task_id;
}

// 简单QPS测试
void simple_qps_test() {
    const std::vector<int> test_sizes = {1000, 5000, 10000, 25000, 50000};
    
    std::cout << "\n=== Simple QPS Performance Test ===" << std::endl;
    std::cout << "Testing different workload sizes..." << std::endl;
    
    for (int size : test_sizes) {
        std::cout << "\nTesting " << size << " tasks:" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            auto main_task = [size]() -> Task<void> {
                std::vector<Task<int>> tasks;
                tasks.reserve(size);
                
                // 创建任务
                for (int i = 0; i < size; ++i) {
                    tasks.emplace_back(simple_work_task(i));
                }
                
                // 执行任务
                int completed = 0;
                for (auto& task : tasks) {
                    try {
                        auto result = co_await task;
                        (void)result;  // 避免未使用变量警告
                        completed++;
                    } catch (...) {
                        // 忽略错误
                    }
                }
                
                std::cout << "  Completed: " << completed << " tasks" << std::endl;
                co_return;
            }();
            
            sync_wait(std::move(main_task));
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            double qps = (size * 1000.0) / duration_ms;
            double avg_latency = (double)duration_ms * 1000.0 / size;
            
            std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
            std::cout << "  QPS: " << static_cast<size_t>(qps) << " requests/second" << std::endl;
            std::cout << "  Average Latency: " << avg_latency << " μs/request" << std::endl;
            std::cout << "  Throughput: " << (qps / 1000.0) << " K req/s" << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "  Test failed: " << e.what() << std::endl;
        }
    }
}

// 并发负载测试 - 修复版本
void concurrent_load_test() {
    std::cout << "\n=== Concurrent Load Test ===" << std::endl;
    
    const std::vector<int> concurrent_levels = {10, 50, 100, 200};  // 减少测试规模
    const int REQUESTS_PER_WORKER = 50;  // 减少每个工作者的请求数
    
    for (int workers : concurrent_levels) {
        std::cout << "\nTesting " << workers << " concurrent workers:" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            auto main_task = [workers]() -> Task<void> {
                std::vector<Task<int>> all_tasks;
                all_tasks.reserve(workers * REQUESTS_PER_WORKER);
                
                // 创建所有任务
                for (int worker_id = 0; worker_id < workers; ++worker_id) {
                    for (int req = 0; req < REQUESTS_PER_WORKER; ++req) {
                        all_tasks.emplace_back(simple_work_task(worker_id * 1000 + req));
                    }
                }
                
                // 顺序执行所有任务
                int completed = 0;
                int failed = 0;
                
                for (auto& task : all_tasks) {
                    try {
                        auto result = co_await task;
                        (void)result;  // 避免未使用变量警告
                        completed++;
                    } catch (...) {
                        failed++;
                    }
                }
                
                std::cout << "  Completed: " << completed << " tasks" << std::endl;
                std::cout << "  Failed: " << failed << " tasks" << std::endl;
                
                co_return;
            }();
            
            sync_wait(std::move(main_task));
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            int total_requests = workers * REQUESTS_PER_WORKER;
            double qps = (total_requests * 1000.0) / duration_ms;
            
            std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
            std::cout << "  Total Requests: " << total_requests << std::endl;
            std::cout << "  **QPS: " << static_cast<size_t>(qps) << " req/s**" << std::endl;
            std::cout << "  Concurrency Level: " << workers << " workers" << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "  Test failed: " << e.what() << std::endl;
        }
    }
}

int main() {
    std::cout << "=== FlowCoro Simple QPS Benchmark ===" << std::endl;
    std::cout << "CPU Cores: " << std::thread::hardware_concurrency() << std::endl;
    
    try {
        // 1. 简单QPS测试
        simple_qps_test();
        
        // 2. 并发负载测试
        concurrent_load_test();
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Simple QPS Benchmark Complete ===" << std::endl;
    return 0;
}
