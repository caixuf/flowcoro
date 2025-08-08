#include "flowcoro.hpp"
#include "test_framework.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace flowcoro;

// 测试基本的 sleep_for 功能
TEST_CASE(basic_sleep_for) {
    std::cout << "测试基本 sleep_for 功能..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << "协程开始执行" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(100));
        std::cout << "协程睡眠100ms后继续执行" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "实际耗时: " << elapsed.count() << "ms" << std::endl;
    
    // 允许一定的时间误差 (±50ms)
    TEST_EXPECT_TRUE(elapsed.count() >= 50 && elapsed.count() <= 200);
    
    std::cout << "基本 sleep_for 测试通过!" << std::endl;
}

TEST_CASE(sequential_sleep_for) {
    std::cout << "测试连续多次 sleep_for..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << "第一次睡眠 50ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(50));
        
        std::cout << "第二次睡眠 30ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(30));
        
        std::cout << "第三次睡眠 20ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(20));
        
        std::cout << "所有睡眠完成" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "总耗时: " << elapsed.count() << "ms" << std::endl;
    
    // 总时间应该接近 50+30+20=100ms
    TEST_EXPECT_TRUE(elapsed.count() >= 80 && elapsed.count() <= 150);
    
    std::cout << "连续 sleep_for 测试通过!" << std::endl;
}
int main() {
    std::cout << "=== FlowCoro sleep_for 专项测试 ===" << std::endl;
    
    // 启动协程管理器
    // start_coroutine_manager();
    
    // 等待协程管理器初始化
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    try {
        // 运行所有测试
        std::cout << "\n1. 基本功能测试" << std::endl;
        test_basic_sleep_for();
        
        std::cout << "\n=== 所有 sleep_for 测试通过! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
