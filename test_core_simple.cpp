#include "include/flowcoro/core_simple.h"
#include <iostream>

using namespace flowcoro;

// 测试基础协程功能
Task<int> simple_test() {
    LOG_INFO("⚡ 测试基础协程功能");
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return 42;
}

// 测试void协程
Task<void> void_test() {
    LOG_INFO("⚡ 测试void协程功能");
    co_await sleep_for(std::chrono::milliseconds(50));
    LOG_INFO("void协程执行完成");
}

int main() {
    std::cout << "\n=== FlowCoro 简化核心测试 ===\n" << std::endl;
    
    try {
        // 测试1：基础功能
        std::cout << "1. 测试基础协程功能..." << std::endl;
        auto result1 = sync_wait(simple_test());
        std::cout << "   ✅ 基础测试结果: " << result1 << std::endl;
        
        // 测试2：void协程
        std::cout << "\n2. 测试void协程..." << std::endl;
        sync_wait(void_test());
        std::cout << "   ✅ void协程测试完成" << std::endl;
        
        // 测试3：嵌套协程
        std::cout << "\n3. 测试嵌套协程..." << std::endl;
        auto nested_task = []() -> Task<std::string> {
            auto inner = []() -> Task<int> {
                co_await sleep_for(std::chrono::milliseconds(30));
                co_return 123;
            };
            
            int value = co_await inner();
            co_return "Nested result: " + std::to_string(value);
        };
        
        auto result3 = sync_wait(nested_task());
        std::cout << "   ✅ 嵌套测试结果: " << result3 << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n🎉 核心功能测试完成!" << std::endl;
    return 0;
}
