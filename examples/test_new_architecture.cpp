#include "../include/flowcoro.hpp"
#include <iostream>
#include <chrono>

using namespace flowcoro;

// 测试基本睡眠功能
Task<void> test_sleep() {
    LOG_INFO("Starting sleep test...");
    
    co_await sleep_for(std::chrono::milliseconds(1000));
    
    LOG_INFO("Sleep test completed!");
    co_return;
}

// 测试多个并发睡眠
Task<void> test_concurrent_sleep() {
    LOG_INFO("Starting concurrent sleep test...");
    
    auto task1 = []() -> Task<void> {
        LOG_INFO("Task 1: Starting 500ms sleep");
        co_await sleep_for(std::chrono::milliseconds(500));
        LOG_INFO("Task 1: Completed");
        co_return;
    }();
    
    auto task2 = []() -> Task<void> {
        LOG_INFO("Task 2: Starting 800ms sleep");
        co_await sleep_for(std::chrono::milliseconds(800));
        LOG_INFO("Task 2: Completed");
        co_return;
    }();
    
    auto task3 = []() -> Task<void> {
        LOG_INFO("Task 3: Starting 300ms sleep");
        co_await sleep_for(std::chrono::milliseconds(300));
        LOG_INFO("Task 3: Completed");
        co_return;
    }();
    
    // 等待所有任务完成
    co_await task1;
    co_await task2;
    co_await task3;
    
    LOG_INFO("All concurrent tasks completed!");
    co_return;
}

// 测试取消功能
Task<void> test_cancellation() {
    LOG_INFO("Starting cancellation test...");
    
    auto long_task = []() -> Task<void> {
        LOG_INFO("Long task: Starting 5 second sleep");
        co_await sleep_for(std::chrono::milliseconds(5000));
        LOG_INFO("Long task: This should not print if cancelled");
        co_return;
    }();
    
    // 等待一小段时间然后取消
    co_await sleep_for(std::chrono::milliseconds(200));
    LOG_INFO("Cancelling long task...");
    long_task.cancel();
    
    co_await sleep_for(std::chrono::milliseconds(100));
    
    if (long_task.is_cancelled()) {
        LOG_INFO("Long task was successfully cancelled");
    } else {
        LOG_ERROR("Long task cancellation failed");
    }
    
    co_return;
}

// 主测试函数
Task<void> run_tests() {
    LOG_INFO("=== FlowCoro 2.0 Architecture Test ===");
    
    try {
        co_await test_sleep();
        co_await test_concurrent_sleep();
        co_await test_cancellation();
        
        LOG_INFO("=== All tests completed successfully! ===");
    } catch (const std::exception& e) {
        LOG_ERROR("Test failed with exception: %s", e.what());
    }
    
    co_return;
}

int main() {
    // 启动协程管理器
    start_coroutine_manager();
    
    // 运行测试
    auto test_task = run_tests();
    
    // 等待测试完成
    sync_wait(std::move(test_task));
    
    LOG_INFO("Program completed");
    return 0;
}
