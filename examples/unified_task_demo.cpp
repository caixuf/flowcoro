#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <thread>

using namespace flowcoro;

// 展示统一的Task<T>接口 - 之前的SafeTask现在就是Task
Task<int> compute_async(int value) {
    co_await sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("Computing value: %d", value);
    co_return value;
}

Task<void> print_message(const std::string& msg) {
    co_await sleep_for(std::chrono::milliseconds(50));
    std::cout << "Message: " << msg << std::endl;
    co_return;
}

Task<int> chain_computation(int input) {
    LOG_INFO("Starting chain computation with: %d", input);
    
    // 简化版本，避免复杂的链式操作
    auto step1 = input * 2;
    LOG_INFO("Step 1 result: %d", step1);
    
    auto step2 = step1 * 2;  
    LOG_INFO("Step 2 result: %d", step2);
    
    co_return step2;
}

Task<int> may_throw_async(bool should_throw) {
    co_await sleep_for(std::chrono::milliseconds(10));
    if (should_throw) {
        throw std::runtime_error("Simulated error in coroutine");
    }
    co_return 42;
}

Task<void> test_exception_handling() {
    std::cout << "=== Testing Exception Handling ===" << std::endl;
    
    try {
        // 成功情况
        auto success_task = may_throw_async(false);
        auto result = success_task.get_result(); // 使用SafeTask兼容方法
        std::cout << "Success result: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Unexpected exception: " << e.what() << std::endl;
    }
    
    try {
        // 异常情况
        auto error_task = may_throw_async(true);
        auto result = error_task.get_result();
        std::cout << "Success result (shouldn't happen): " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Caught expected exception: " << e.what() << std::endl;
    }
    
    co_return;
}

int main() {
    std::cout << "=== FlowCoro Unified Task Demo ===" << std::endl;
    std::cout << "Note: SafeTask is now just an alias for Task<T>" << std::endl;
    std::cout << std::endl;

    std::cout << "=== Basic Coroutine Test ===" << std::endl;
    auto basic_task = compute_async(42);
    // 展示Task的is_ready()方法（来自SafeTask）
    std::cout << "Task ready before execution: " << std::boolalpha << basic_task.is_ready() << std::endl;
    auto result = basic_task.get_result();
    std::cout << "Basic computation result: " << result << std::endl;
    std::cout << std::endl;

    std::cout << "=== Chain Computation Test ===" << std::endl;
    auto chain_task = chain_computation(5);
    auto chain_result = chain_task.get_result();
    std::cout << "Chain computation result: " << chain_result << std::endl;
    std::cout << std::endl;

    // 异步执行测试（演示统一接口）
    auto exception_task = test_exception_handling();
    exception_task.get_result(); // void版本的get_result()
    
    std::cout << std::endl;
    std::cout << "=== Demonstrating Task/SafeTask Equivalence ===" << std::endl;
    
    // 显示Task和SafeTask是同一类型
    Task<int> task1 = compute_async(100);
    SafeTask<int> task2 = compute_async(200);  // SafeTask是Task的别名
    
    // 两者完全等价
    static_assert(std::is_same_v<Task<int>, SafeTask<int>>, "Task and SafeTask should be the same type!");
    
    auto result1 = task1.get();  // 使用Task的原有方法
    auto result2 = task2.get_result(); // 使用SafeTask风格的方法
    
    std::cout << "Task<int> result: " << result1 << std::endl;
    std::cout << "SafeTask<int> result: " << result2 << std::endl;
    
    std::cout << std::endl;
    std::cout << "=== All tests completed successfully! ===" << std::endl;
    
    // 添加适当的清理延时，让线程池正常清理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}
