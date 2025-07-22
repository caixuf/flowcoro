#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <thread>

using namespace flowcoro;

// 展示统一的Task<T>接口 - 之前的SafeTask现在就是Task
Task<int> simple_compute(int value) {
    // 简单的同步计算，避免复杂的异步操作
    LOG_INFO("Computing value: %d", value);
    co_return value * 2;
}

int main() {
    std::cout << "=== FlowCoro Unified Task Demo ===" << std::endl;
    std::cout << "Note: SafeTask is now just an alias for Task<T>" << std::endl;
    std::cout << std::endl;
    
    // 基本测试
    std::cout << "=== Basic Task Test ===" << std::endl;
    auto task = simple_compute(42);
    std::cout << "Task ready: " << task.is_ready() << std::endl;
    auto result = task.get();
    std::cout << "Basic computation result: " << result << std::endl;
    std::cout << std::endl;
    
    // Task/SafeTask等价性测试
    std::cout << "=== Demonstrating Task/SafeTask Equivalence ===" << std::endl;
    
    // 编译时类型检查
    static_assert(std::is_same_v<Task<int>, SafeTask<int>>, 
                  "Task and SafeTask should be the same type!");
    
    // 创建两个任务，使用不同的类型别名
    Task<int> task1 = simple_compute(100);
    SafeTask<int> task2 = simple_compute(200);  // SafeTask是Task的别名
    
    // 使用不同的API风格
    auto result1 = task1.get();         // Task的原有方法
    auto result2 = task2.get_result();  // SafeTask风格的方法 (现在是Task的兼容方法)
    
    std::cout << "Task<int> result: " << result1 << std::endl;
    std::cout << "SafeTask<int> result: " << result2 << std::endl;
    std::cout << std::endl;
    
    std::cout << "=== Type equivalence confirmed! ===" << std::endl;
    std::cout << "Both Task<T> and SafeTask<T> are exactly the same type." << std::endl;
    std::cout << "SafeTask is just a convenient alias for backward compatibility." << std::endl;
    
    return 0;  // 简洁退出，避免线程池问题
}
