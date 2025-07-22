#include "flowcoro/core.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace flowcoro;

// 基本的协程函数示例
SafeTask<int> compute_async(int value) {
    // 模拟异步计算
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return value * 2;
}

// void返回类型的协程
SafeTask<> print_message(const std::string& msg) {
    std::cout << "Message: " << msg << std::endl;
    co_return;
}

// 协程链式调用示例
SafeTask<int> chain_computation(int input) {
    std::cout << "Starting chain computation with: " << input << std::endl;
    
    // co_await其他协程任务
    int step1 = co_await compute_async(input);
    std::cout << "Step 1 result: " << step1 << std::endl;
    
    int step2 = co_await compute_async(step1);
    std::cout << "Step 2 result: " << step2 << std::endl;
    
    // 调用void协程
    co_await print_message("Chain computation completed");
    
    co_return step2;
}

// 异常处理示例
SafeTask<int> may_throw_async(bool should_throw) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    if (should_throw) {
        throw std::runtime_error("Simulated error in coroutine");
    }
    
    co_return 42;
}

SafeTask<> test_exception_handling() {
    std::cout << "\n=== Testing Exception Handling ===" << std::endl;
    
    try {
        int result = co_await may_throw_async(false);
        std::cout << "Success result: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Caught exception (shouldn't happen): " << e.what() << std::endl;
    }
    
    try {
        int result = co_await may_throw_async(true);
        std::cout << "Success result (shouldn't happen): " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Caught expected exception: " << e.what() << std::endl;
    }
    
    co_return;
}

int main() {
    std::cout << "=== FlowCoro SafeTask Demo ===" << std::endl;
    
    try {
        // 测试基本协程功能
        std::cout << "\n=== Basic Coroutine Test ===" << std::endl;
        auto basic_task = compute_async(21);
        int basic_result = basic_task.get_result();
        std::cout << "Basic computation result: " << basic_result << std::endl;
        
        // 测试链式计算
        std::cout << "\n=== Chain Computation Test ===" << std::endl;
        auto chain_task = chain_computation(5);
        int chain_result = chain_task.get_result();
        std::cout << "Chain computation result: " << chain_result << std::endl;
        
        // 测试异常处理
        auto exception_task = test_exception_handling();
        exception_task.get_result();
        
        std::cout << "\n=== All tests completed successfully! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in main: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
