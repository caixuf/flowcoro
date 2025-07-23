#include "../include/flowcoro.hpp"
#include <iostream>
#include <chrono>

using namespace flowcoro;

// 测试协程任务
Task<int> simple_task(int value) {
    std::cout << "协程 " << value << " 开始执行" << std::endl;
    
    // 模拟一些工作
    co_await sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "协程 " << value << " 执行完成" << std::endl;
    co_return value * 2;
}

// 测试并发任务 - 简化版
Task<void> test_concurrent_tasks() {
    std::cout << "=== 测试并发协程任务 ===" << std::endl;
    
    // 创建并启动所有任务
    std::vector<Task<int>> tasks;
    for (int i = 1; i <= 5; ++i) {
        tasks.push_back(simple_task(i));
    }
    
    auto& manager = CoroutineManager::get_instance();
    
    // 启动所有任务
    for (auto& task : tasks) {
        manager.schedule_resume(task.handle);
    }
    
    // 等待所有任务完成并获取结果
    for (int i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];
        
        // 等待任务完成
        while (!task.handle.done()) {
            manager.drive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        int result = task.get();
        std::cout << "任务结果: " << result << std::endl;
    }
    
    co_return;
}

// 测试协程池统计 - 简化版
Task<void> test_pool_stats() {
    std::cout << "\n=== 协程池统计信息 ===" << std::endl;
    
    // 打印初始统计
    print_pool_stats();
    
    // 创建一些任务
    auto task1 = simple_task(100);
    auto task2 = simple_task(200);
    
    auto& manager = CoroutineManager::get_instance();
    
    // 启动任务
    manager.schedule_resume(task1.handle);
    manager.schedule_resume(task2.handle);
    
    // 等待完成
    while (!task1.handle.done()) {
        manager.drive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    while (!task2.handle.done()) {
        manager.drive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 打印最终统计
    std::cout << "\n任务完成后的统计：" << std::endl;
    print_pool_stats();
    
    co_return;
}

int main() {
    std::cout << "FlowCoro 协程池演示程序" << std::endl;
    std::cout << "版本: " << FLOWCORO_VERSION_STRING << std::endl << std::endl;
    
    try {
        // 测试并发任务
        auto concurrent_test = test_concurrent_tasks();
        run_until_complete(concurrent_test);
        
        // 等待一下
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 测试统计信息
        auto stats_test = test_pool_stats();
        run_until_complete(stats_test);
        
        std::cout << "\n=== 演示完成 ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
