#include "../include/flowcoro.hpp"
#include <iostream>
#include <vector>

using namespace flowcoro;

// 简单的协程任务
Task<int> simple_task(int value) {
    std::cout << "任务 " << value << " 开始" << std::endl;
    
    // 模拟工作
    co_await sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "任务 " << value << " 完成" << std::endl;
    co_return value * 2;
}

// 简单的void任务
Task<void> simple_void_task(int id) {
    std::cout << "Void任务 " << id << " 开始" << std::endl;
    co_await sleep_for(std::chrono::milliseconds(30));
    std::cout << "Void任务 " << id << " 完成" << std::endl;
}

int main() {
    std::cout << "FlowCoro 简化协程池测试" << std::endl;
    
    try {
        // 测试1: 单个任务
        std::cout << "\n=== 测试1: 单个任务 ===" << std::endl;
        auto task1 = simple_task(1);
        // 改用直接等待而不是sync_wait
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(task1.handle);
        while (!task1.handle.done()) {
            manager.drive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto result = task1.get();
        std::cout << "单个任务结果: " << result << std::endl;
        
        // 测试2: void任务
        std::cout << "\n=== 测试2: Void任务 ===" << std::endl;
        auto void_task = simple_void_task(1);
        manager.schedule_resume(void_task.handle);
        while (!void_task.handle.done()) {
            manager.drive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void_task.get();
        std::cout << "Void任务完成" << std::endl;
        
        // 测试3: 多个任务顺序执行
        std::cout << "\n=== 测试3: 多个任务顺序执行 ===" << std::endl;
        for (int i = 1; i <= 3; ++i) {
            auto task = simple_task(i);
            manager.schedule_resume(task.handle);
            while (!task.handle.done()) {
                manager.drive();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto result = task.get();
            std::cout << "任务 " << i << " 结果: " << result << std::endl;
        }
        
        std::cout << "\n=== 测试完成 ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
