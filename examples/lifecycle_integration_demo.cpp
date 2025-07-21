/**
 * @file lifecycle_integration_demo.cpp
 * @brief FlowCoro协程生命周期管理集成演示
 * @author FlowCoro Team  
 * @date 2025-07-21
 * 
 * 演示增强的协程生命周期管理功能：
 * 1. 安全的协程句柄管理
 * 2. 取消机制
 * 3. 超时处理  
 * 4. 状态追踪
 * 5. 与现有Task的兼容性
 */

#include "flowcoro/lifecycle.h"
#include "flowcoro/core.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <iomanip>

using namespace flowcoro;
using namespace flowcoro::lifecycle;
using namespace std::chrono_literals;

// 演示1: 基本的增强任务功能
enhanced_task<int> basic_computation(int value, std::chrono::milliseconds delay) {
    coroutine_guard guard;  // 自动生命周期管理
    
    LOG_INFO("basic_computation: Starting computation with value %d", value);
    
    // 模拟计算工作
    co_await std::suspend_always{};  // 挂起点
    
    // 模拟状态检查（这里简化为直接继续）
    
    std::this_thread::sleep_for(delay);
    
    int result = value * 2;
    LOG_INFO("basic_computation: Completed with result %d", result);
    
    co_return result;
}

// 演示2: 可取消的长时间运行任务
enhanced_task<std::string> long_running_task(int iterations) {
    coroutine_guard guard;
    
    LOG_INFO("long_running_task: Starting %d iterations", iterations);
    
    for (int i = 0; i < iterations; ++i) {
        // 在每次迭代时检查取消状态
        co_await std::suspend_always{};
        
        std::this_thread::sleep_for(100ms);
        
        if (i % 10 == 0) {
            LOG_INFO("long_running_task: Progress %d/%d", i, iterations);
        }
    }
    
    LOG_INFO("long_running_task: Completed all iterations");
    co_return "Task completed successfully";
}

// 演示3: 与原有Task兼容的函数
Task<double> legacy_task(double value) {
    LOG_INFO("legacy_task: Processing value %f", value);
    std::this_thread::sleep_for(200ms);
    co_return value * 3.14159;
}

// 演示4: 错误处理
enhanced_task<int> error_prone_task(bool should_fail) {
    coroutine_guard guard;
    
    LOG_INFO("error_prone_task: Starting, should_fail=%s", should_fail ? "true" : "false");
    
    if (should_fail) {
        guard.mark_failed();
        throw std::runtime_error("Simulated failure");
    }
    
    co_return 42;
}

// 工具函数：打印统计信息
void print_stats(const std::string& stage) {
    auto stats = get_coroutine_stats();
    
    std::cout << "\n=== " << stage << " ===\n";
    std::cout << "Active coroutines: " << stats.active_coroutines << "\n";
    std::cout << "Total created: " << stats.total_created << "\n";  
    std::cout << "Completed: " << stats.completed_coroutines << "\n";
    std::cout << "Cancelled: " << stats.cancelled_coroutines << "\n";
    std::cout << "Failed: " << stats.failed_coroutines << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Completion rate: " << (stats.completion_rate * 100) << "%\n";
    std::cout << "Failure rate: " << (stats.failure_rate * 100) << "%\n";
    std::cout << "Cancellation rate: " << (stats.cancellation_rate * 100) << "%\n";
    std::cout << std::string(40, '-') << "\n";
}

Task<void> demo_basic_functionality() {
    LOG_INFO("=== Demo 1: Basic Enhanced Task Functionality ===");
    
    // 创建几个基本任务
    auto task1 = basic_computation(10, 500ms);
    auto task2 = basic_computation(20, 300ms); 
    auto task3 = basic_computation(30, 200ms);
    
    print_stats("After Creating Basic Tasks");
    
    // 等待任务完成
    auto result1 = co_await task1;
    auto result2 = co_await task2;  
    auto result3 = co_await task3;
    
    LOG_INFO("Basic task results: %d, %d, %d", result1, result2, result3);
    
    print_stats("After Basic Tasks Completion");
}

Task<void> demo_cancellation() {
    LOG_INFO("=== Demo 2: Cancellation Mechanism ===");
    
    cancellation_source cancel_source;
    auto cancel_token = cancel_source.get_token();
    
    // 创建一个长时间运行的任务
    auto long_task = long_running_task(50);
    long_task.set_cancellation_token(cancel_token);
    
    // 在另一个线程中等待一段时间后取消
    std::thread canceller([&cancel_source]() {
        std::this_thread::sleep_for(1s);
        LOG_INFO("Requesting cancellation...");
        cancel_source.cancel();
    });
    
    try {
        auto result = co_await long_task;
        LOG_INFO("Long task result: %s", result.c_str());
    } catch (const operation_cancelled_exception& e) {
        LOG_INFO("Task was cancelled: %s", e.what());
    }
    
    canceller.join();
    print_stats("After Cancellation Demo");
}

Task<void> demo_timeout() {
    LOG_INFO("=== Demo 3: Timeout Handling ===");
    
    // 创建一个会超时的任务
    auto slow_task = long_running_task(100);  // 需要约10秒
    
    try {
        // 设置2秒超时
        auto result = co_await with_timeout(std::move(slow_task), 2s);
        LOG_INFO("Timeout task result: %s", result.c_str());
    } catch (const std::exception& e) {
        LOG_INFO("Task timed out: %s", e.what());
    }
    
    print_stats("After Timeout Demo");
}

Task<void> demo_compatibility() {
    LOG_INFO("=== Demo 4: Legacy Compatibility ===");
    
    // 演示与原有Task的兼容性
    auto legacy = legacy_task(2.5);
    auto result = co_await legacy;
    
    LOG_INFO("Legacy task result: %f", result);
    
    print_stats("After Legacy Compatibility Demo");
}

Task<void> demo_error_handling() {
    LOG_INFO("=== Demo 5: Error Handling ===");
    
    // 成功的任务
    try {
        auto success_task = error_prone_task(false);
        auto result = co_await success_task;
        LOG_INFO("Success task result: %d", result);
    } catch (const std::exception& e) {
        LOG_ERROR("Unexpected error: %s", e.what());
    }
    
    // 失败的任务
    try {
        auto fail_task = error_prone_task(true);
        auto result = co_await fail_task;
        LOG_INFO("Fail task result: %d", result);
    } catch (const std::exception& e) {
        LOG_INFO("Expected error caught: %s", e.what());
    }
    
    print_stats("After Error Handling Demo");
}

Task<void> demo_concurrent_tasks() {
    LOG_INFO("=== Demo 6: Concurrent Task Management ===");
    
    std::vector<enhanced_task<int>> tasks;
    
    // 创建多个并发任务
    for (int i = 0; i < 5; ++i) {
        tasks.emplace_back(basic_computation(i + 1, 200ms + std::chrono::milliseconds(i * 100)));
    }
    
    print_stats("After Creating Concurrent Tasks");
    
    // 等待所有任务完成
    std::vector<int> results;
    for (auto& task : tasks) {
        try {
            auto result = co_await task;
            results.push_back(result);
        } catch (const std::exception& e) {
            LOG_ERROR("Task failed: %s", e.what());
            results.push_back(-1);
        }
    }
    
    LOG_INFO("Concurrent task results:");
    for (size_t i = 0; i < results.size(); ++i) {
        LOG_INFO("  Task %zu: %d", i, results[i]);
    }
    
    print_stats("After Concurrent Tasks Completion");
}

Task<void> run_all_demos() {
    LOG_INFO("FlowCoro协程生命周期管理集成演示开始");
    print_stats("Initial State");
    
    co_await demo_basic_functionality();
    co_await demo_cancellation();
    co_await demo_timeout();
    co_await demo_compatibility();
    co_await demo_error_handling();
    co_await demo_concurrent_tasks();
    
    print_stats("Final State");
    LOG_INFO("所有演示完成！");
}

int main() {
    try {
        LOG_INFO("Starting FlowCoro Lifecycle Integration Demo");
        
        // 运行所有演示
        auto demo_task = run_all_demos();
        demo_task.get();  // Task<void>的get()返回void
        
        // 最终统计
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "演示总结:\n";
        auto final_stats = get_coroutine_stats();
        std::cout << "- 总共创建了 " << final_stats.total_created << " 个协程\n";
        std::cout << "- 成功完成 " << final_stats.completed_coroutines << " 个\n";
        std::cout << "- 被取消 " << final_stats.cancelled_coroutines << " 个\n";
        std::cout << "- 执行失败 " << final_stats.failed_coroutines << " 个\n";
        std::cout << "- 成功率: " << std::fixed << std::setprecision(1) 
                  << (final_stats.completion_rate * 100) << "%\n";
        
        // 清理
        GlobalThreadPool::shutdown();
        
        LOG_INFO("Demo completed successfully!");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Demo failed with exception: %s", e.what());
        return 1;
    } catch (...) {
        LOG_ERROR("Demo failed with unknown exception");
        return 1;
    }
}
