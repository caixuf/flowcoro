#include <iostream>
#include <chrono>
#include <string>
#include <any>
#include <cmath>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

// 测试任务：使用sleep_for的协程
Task<int> fast_task() {
    // 使用sleep_for进行异步等待
    std::cout << "快速任务开始睡眠...\n";
    co_await sleep_for(std::chrono::milliseconds(50));
    std::cout << "快速任务完成！\n";
    co_return 42;
}

Task<std::string> slow_task() {
    // 使用sleep_for进行异步等待
    std::cout << "慢速任务开始睡眠...\n";
    co_await sleep_for(std::chrono::milliseconds(500));
    std::cout << "慢速任务完成！\n";
    co_return std::string("slow_result");
}

Task<double> medium_task() {
    // 使用sleep_for进行异步等待
    std::cout << "中速任务开始睡眠...\n";
    co_await sleep_for(std::chrono::milliseconds(200));
    std::cout << "中速任务完成！\n";
    co_return 3.14;
}

// 测试when_any基本功能
TEST_CASE(when_any_basic) {
    std::cout << "\n=== when_any 基本功能测试 ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto start = high_resolution_clock::now();
        
        // 启动三个不同速度的任务
        auto result = co_await when_any(fast_task(), slow_task(), medium_task());
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << "完成时间: " << duration.count() << "ms\n";
        std::cout << "获胜者索引: " << result.first << "\n";
        
        // 快速任务应该获胜（索引0）
        TEST_EXPECT_EQ(result.first, 0);
        
        // 检查结果类型
        if (result.first == 0) {
            auto value = std::any_cast<int>(result.second);
            std::cout << "快速任务获胜，结果: " << value << "\n";
            TEST_EXPECT_EQ(value, 42);
        }
        
        // 验证时间大致正确（快速任务应该在50ms左右完成）
        TEST_EXPECT_TRUE(duration.count() < 150); // 给一些余量
        
        std::cout << "✅ when_any基本测试完成\n";
    }();
    
    sync_wait(std::move(test_coro));
}

// 测试when_any竞争条件
TEST_CASE(when_any_race) {
    std::cout << "\n=== when_any 竞争测试 ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto create_racing_task = [](int id, int computation_size) -> Task<int> {
            // 根据 computation_size 进行不同强度的计算
            volatile int sum = 0;
            for (int i = 0; i < computation_size; ++i) {
                sum += i;
            }
            std::cout << "任务 " << id << " 完成 (计算量: " << computation_size << ")\n";
            co_return id;
        };
        
        // 创建多个接近的竞争任务
        auto start = high_resolution_clock::now();
        
        auto result = co_await when_any(
            create_racing_task(1, 50000),    // 最少计算量
            create_racing_task(2, 100000),   // 中等计算量
            create_racing_task(3, 150000),   // 较多计算量
            create_racing_task(4, 200000)    // 最多计算量
        );
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << "竞争完成时间: " << duration.count() << "ms\n";
        std::cout << "获胜者: 任务 " << std::any_cast<int>(result.second) << " (索引: " << result.first << ")\n";
        
        // 最快的任务应该获胜
        TEST_EXPECT_EQ(result.first, 0);
        TEST_EXPECT_EQ(std::any_cast<int>(result.second), 1);
        
        std::cout << "✅ 竞争测试完成\n";
    }();
    
    sync_wait(std::move(test_coro));
}

// 测试when_any的类型安全
TEST_CASE(when_any_type_safety) {
    std::cout << "\n=== when_any 类型安全测试 ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto int_task = []() -> Task<int> {
            volatile int sum = 0;
            for (int i = 0; i < 30000; ++i) {
                sum += i;
            }
            co_return 123;
        };
        
        auto string_task = []() -> Task<std::string> {
            volatile int sum = 0;
            for (int i = 0; i < 100000; ++i) {
                sum += i;
            }
            co_return std::string("hello");
        };
        
        auto bool_task = []() -> Task<bool> {
            volatile int sum = 0;
            for (int i = 0; i < 150000; ++i) {
                sum += i;
            }
            co_return true;
        };
        
        auto result = co_await when_any(int_task(), string_task(), bool_task());
        
        std::cout << "获胜者索引: " << result.first << "\n";
        TEST_EXPECT_EQ(result.first, 0); // int_task应该最快
        
        // 验证结果类型
        if (result.first == 0) {
            auto value = std::any_cast<int>(result.second);
            std::cout << "int任务获胜，结果: " << value << "\n";
            TEST_EXPECT_EQ(value, 123);
        } else if (result.first == 1) {
            auto value = std::any_cast<std::string>(result.second);
            std::cout << "string任务获胜，结果: " << value << "\n";
            TEST_EXPECT_EQ(value, "hello");
        } else if (result.first == 2) {
            auto value = std::any_cast<bool>(result.second);
            std::cout << "bool任务获胜，结果: " << value << "\n";
            TEST_EXPECT_EQ(value, true);
        }
        
        std::cout << "✅ 类型安全测试完成\n";
    }();
    
    sync_wait(std::move(test_coro));
}

int main() {
    std::cout << "FlowCoro when_any 功能测试开始..." << std::endl;
    
    try {
        test_when_any_basic();
        test_when_any_race();
        test_when_any_type_safety();
    
        
        std::cout << "\n所有when_any测试通过！\n";
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
