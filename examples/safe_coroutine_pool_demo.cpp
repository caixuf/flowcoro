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

// 安全的并发任务测试
Task<void> safe_concurrent_test() {
    std::cout << "=== 安全的并发协程任务测试 ===" << std::endl;
    
    // 创建任务
    auto task1 = simple_task(1);
    auto task2 = simple_task(2);
    auto task3 = simple_task(3);
    
    // 使用协程等待，而不是手动调度
    int result1 = co_await task1;
    int result2 = co_await task2;
    int result3 = co_await task3;
    
    std::cout << "结果: " << result1 << ", " << result2 << ", " << result3 << std::endl;
}

// 安全的统计信息测试
Task<void> safe_stats_test() {
    std::cout << "\n=== 安全的协程池统计测试 ===" << std::endl;
    
    // 打印统计
    print_pool_stats();
    
    // 创建并等待简单任务
    auto task = simple_task(100);
    int result = co_await task;
    
    std::cout << "任务结果: " << result << std::endl;
    
    // 最终统计
    std::cout << "\n最终统计:" << std::endl;
    print_pool_stats();
}

int main() {
    std::cout << "FlowCoro 安全协程池演示程序" << std::endl;
    std::cout << "版本: " << FLOWCORO_VERSION_STRING << std::endl << std::endl;
    
    try {
        // 测试并发任务
        auto concurrent_test = safe_concurrent_test();
        run_until_complete(concurrent_test);
        
        // 等待一下
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 测试统计信息
        auto stats_test = safe_stats_test();
        run_until_complete(stats_test);
        
        std::cout << "\n=== 演示完成 ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
