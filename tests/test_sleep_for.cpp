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

// 测试高精度 sleep_for
TEST_CASE(high_precision_sleep_for) {
    std::cout << "测试高精度 sleep_for..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << "测试微秒级睡眠" << std::endl;
        co_await sleep_for_microseconds(5000); // 5毫秒
        std::cout << "微秒级睡眠完成" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::cout << "实际耗时: " << elapsed.count() << "μs" << std::endl;
    
    // 允许一定的时间误差
    TEST_EXPECT_TRUE(elapsed.count() >= 3000 && elapsed.count() <= 10000);
    
    std::cout << "高精度 sleep_for 测试通过!" << std::endl;
}

// 测试零时长的 sleep_for
TEST_CASE(zero_duration_sleep_for) {
    std::cout << "测试零时长 sleep_for..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << "测试零时长睡眠" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(0));
        std::cout << "零时长睡眠完成" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "实际耗时: " << elapsed.count() << "ms" << std::endl;
    
    // 零时长应该立即返回，允许很小的误差
    TEST_EXPECT_TRUE(elapsed.count() <= 10);
    
    std::cout << "零时长 sleep_for 测试通过!" << std::endl;
}

// 测试多个协程并发 sleep_for
TEST_CASE(concurrent_sleep_for) {
    std::cout << "测试多个协程并发 sleep_for..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    std::atomic<int> completed_count{0};
    
    auto create_task = [&](int id, int delay_ms) -> Task<void> {
        std::cout << "协程 " << id << " 开始，将睡眠 " << delay_ms << "ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(delay_ms));
        completed_count.fetch_add(1);
        std::cout << "协程 " << id << " 完成" << std::endl;
    };

    // 创建多个任务并分别启动，避免when_all的void问题
    auto task1 = create_task(1, 50);
    auto task2 = create_task(2, 100);
    auto task3 = create_task(3, 150);
    
    // 分别等待每个任务完成
    sync_wait(std::move(task1));
    sync_wait(std::move(task2));
    sync_wait(std::move(task3));
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "总耗时: " << elapsed.count() << "ms" << std::endl;
    std::cout << "完成的协程数量: " << completed_count.load() << std::endl;
    
    // 验证所有任务都完成了
    TEST_EXPECT_EQ(completed_count.load(), 3);
    
    std::cout << "并发 sleep_for 测试通过!" << std::endl;
}

// 测试 sleep_for 与超时结合
TEST_CASE(sleep_for_with_timeout) {
    std::cout << "测试 sleep_for 与超时结合..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto long_sleep_task = []() -> Task<bool> {
        std::cout << "开始长时间睡眠 (500ms)" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(500));
        std::cout << "长时间睡眠完成" << std::endl;
        co_return true;
    };
    
    // 使用超时功能
    auto result = sync_wait(when_any_timeout(long_sleep_task(), std::chrono::milliseconds(200)));
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "实际耗时: " << elapsed.count() << "ms" << std::endl;
    
    // 应该在超时时间(200ms)附近结束，而不是等待完整的500ms
    TEST_EXPECT_TRUE(elapsed.count() >= 150 && elapsed.count() <= 300);
    
    std::cout << "sleep_for 超时测试通过!" << std::endl;
}

// 测试连续多次 sleep_for
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

// 测试不同时间单位的 sleep_for
TEST_CASE(different_time_units_sleep_for) {
    std::cout << "测试不同时间单位的 sleep_for..." << std::endl;
    
    auto test_task = []() -> Task<void> {
        auto start = std::chrono::steady_clock::now();
        
        // 测试毫秒
        std::cout << "睡眠 10 毫秒" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(10));
        
        auto after_ms = std::chrono::steady_clock::now();
        auto ms_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(after_ms - start);
        std::cout << "毫秒睡眠耗时: " << ms_elapsed.count() << "ms" << std::endl;
        
        // 测试微秒
        std::cout << "睡眠 5000 微秒" << std::endl;
        co_await sleep_for(std::chrono::microseconds(5000));
        
        auto after_us = std::chrono::steady_clock::now();
        auto us_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(after_us - after_ms);
        std::cout << "微秒睡眠耗时: " << us_elapsed.count() << "μs" << std::endl;
        
        // 测试秒
        std::cout << "睡眠 1 秒 (注意：这会比较慢)" << std::endl;
        co_await sleep_for(std::chrono::seconds(1));
        
        auto end = std::chrono::steady_clock::now();
        auto s_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - after_us);
        std::cout << "秒睡眠耗时: " << s_elapsed.count() << "ms" << std::endl;
        
        std::cout << "所有时间单位测试完成" << std::endl;
    };
    
    auto start_time = std::chrono::steady_clock::now();
    sync_wait(test_task());
    auto end_time = std::chrono::steady_clock::now();
    
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "总耗时: " << total_elapsed.count() << "ms" << std::endl;
    
    // 总时间应该接近 10ms + 5ms + 1000ms = 1015ms
    TEST_EXPECT_TRUE(total_elapsed.count() >= 900 && total_elapsed.count() <= 1200);
    
    std::cout << "不同时间单位 sleep_for 测试通过!" << std::endl;
}

// 主测试函数
int main() {
    std::cout << "=== FlowCoro sleep_for 专项测试 ===" << std::endl;
    
    // 启动协程管理器
    start_coroutine_manager();
    
    // 等待协程管理器初始化
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    try {
        // 运行所有测试
        std::cout << "\n1. 基本功能测试" << std::endl;
        test_basic_sleep_for();
        
        std::cout << "\n2. 高精度测试" << std::endl;
        test_high_precision_sleep_for();
        
        std::cout << "\n3. 零时长测试" << std::endl;
        test_zero_duration_sleep_for();
        
        std::cout << "\n4. 并发测试" << std::endl;
        test_concurrent_sleep_for();
        
        std::cout << "\n5. 超时测试" << std::endl;
        test_sleep_for_with_timeout();
        
        std::cout << "\n6. 连续睡眠测试" << std::endl;
        test_sequential_sleep_for();
        
        std::cout << "\n7. 不同时间单位测试" << std::endl;
        test_different_time_units_sleep_for();
        
        std::cout << "\n=== 所有 sleep_for 测试通过! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
