#include "../include/flowcoro.hpp"
#include <iostream>
#include <chrono>

using namespace flowcoro;

// 测试基本睡眠功能
Task<void> test_sleep() {
    std::cout << "Starting sleep test..." << std::endl;
    
    co_await sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "Sleep test completed!" << std::endl;
    co_return;
}

// 简单测试函数
Task<void> run_simple_test() {
    std::cout << "=== FlowCoro 2.0 Architecture Test ===" << std::endl;
    
    try {
        co_await test_sleep();
        std::cout << "=== Test completed successfully! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
    }
    
    co_return;
}

int main() {
    std::cout << "Starting FlowCoro 2.0 test..." << std::endl;
    
    // 启动协程管理器
    start_coroutine_manager();
    
    // 运行测试
    auto test_task = run_simple_test();
    
    // 等待测试完成
    sync_wait(std::move(test_task));
    
    std::cout << "Program completed" << std::endl;
    return 0;
}
