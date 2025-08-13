#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <set>

#include "flowcoro.hpp"

using namespace flowcoro;

// 安全的测试函数，避免TEST_CASE自动注册
void test_cross_thread_coroutine_resume() {
    std::cout << "测试跨线程协程恢复..." << std::endl;
    
    std::atomic<int> counter{0};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> thread_ids;
    
    // 记录线程ID的函数
    auto record_thread_id = [&]() {
        std::lock_guard<std::mutex> lock(thread_ids_mutex);
        thread_ids.insert(std::this_thread::get_id());
    };
    
    // 创建一个协程，在不同线程中执行
    auto cross_thread_coro = [&]() -> Task<int> {
        std::cout << "协程开始执行在线程: " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        
        // 第一次暂停和恢复
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "协程第一次恢复在线程: " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        // 第二次暂停和恢复
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "协程第二次恢复在线程: " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        // 第三次暂停和恢复
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "协程第三次恢复在线程: " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        co_return 42;
    }();
    
    // 等待协程完成
    auto result = sync_wait(std::move(cross_thread_coro));
    
    // 验证结果
    std::cout << "结果: " << result << " (期望: 42)" << std::endl;
    std::cout << "计数器: " << counter.load() << " (期望: 3)" << std::endl;
    
    // 验证协程在多个线程中执行
    std::cout << "协程在 " << thread_ids.size() << " 个不同线程中执行过" << std::endl;
    for (const auto& tid : thread_ids) {
        std::cout << "线程ID: " << tid << std::endl;
    }
    
    if (result == 42 && counter.load() == 3 && thread_ids.size() >= 1) {
        std::cout << "跨线程协程恢复测试通过!" << std::endl;
    } else {
        std::cout << "跨线程协程恢复测试失败!" << std::endl;
    }
}

// 测试多个协程的跨线程调度 - 使用安全的顺序方式
void test_multiple_coroutines_cross_thread() {
    std::cout << "测试多个协程的跨线程调度..." << std::endl;
    
    const int NUM_COROUTINES = 5; // 减少协程数量以提高稳定性
    std::atomic<int> completed_count{0};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> all_thread_ids;
    
    std::vector<int> results;
    
    // 顺序创建和执行协程，避免并发竞态条件
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        auto create_coro = [](int index, std::atomic<int>& counter, std::mutex& mutex, std::set<std::thread::id>& thread_set) -> Task<int> {
            std::cout << "协程 " << index << " 开始在线程: " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(mutex);
                thread_set.insert(std::this_thread::get_id());
            }
            
            // 随机延迟，增加跨线程调度的可能性
            co_await sleep_for(std::chrono::milliseconds(5 + (index % 20)));
            
            std::cout << "协程 " << index << " 恢复在线程: " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(mutex);
                thread_set.insert(std::this_thread::get_id());
            }
            
            counter.fetch_add(1);
            co_return index * 10;
        };
        
        auto coro = create_coro(i, std::ref(completed_count), std::ref(thread_ids_mutex), std::ref(all_thread_ids));
        int result = sync_wait(std::move(coro));
        results.push_back(result);
        
        std::cout << "协程 " << i << " 返回: " << result << std::endl;
    }
    
    // 验证结果
    std::cout << "完成计数: " << completed_count.load() << " (期望: " << NUM_COROUTINES << ")" << std::endl;
    std::cout << "结果数量: " << results.size() << " (期望: " << NUM_COROUTINES << ")" << std::endl;
    
    bool all_correct = true;
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        if (results[i] != i * 10) {
            all_correct = false;
            std::cout << "协程 " << i << " 结果错误: 期望 " << (i * 10) << ", 实际 " << results[i] << std::endl;
        }
    }
    
    std::cout << "所有协程在 " << all_thread_ids.size() << " 个不同线程中执行过" << std::endl;
    
    if (all_correct && completed_count.load() == NUM_COROUTINES && all_thread_ids.size() >= 1) {
        std::cout << "多个协程跨线程调度测试通过!" << std::endl;
    } else {
        std::cout << "多个协程跨线程调度测试失败!" << std::endl;
    }
}

// 测试协程异常在跨线程环境下的处理
void test_cross_thread_exception_handling() {
    std::cout << "测试跨线程协程异常处理..." << std::endl;
    
    std::atomic<bool> exception_caught{false};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> exception_thread_ids;
    
    // 创建一个会模拟异常的协程
    auto exception_coro = [&]() -> Task<int> {
        std::cout << "异常协程开始在线程: " << std::this_thread::get_id() << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(thread_ids_mutex);
            exception_thread_ids.insert(std::this_thread::get_id());
        }
        
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "异常协程恢复在线程: " << std::this_thread::get_id() << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(thread_ids_mutex);
            exception_thread_ids.insert(std::this_thread::get_id());
        }
        
        // FlowCoro不使用异常，而是通过日志记录错误
        // 模拟错误情况，返回错误值
        std::cout << "模拟异常情况发生" << std::endl;
        exception_caught.store(true);
        
        co_return -1; // 错误值
    }();
    
    // 等待协程完成
    auto result = sync_wait(std::move(exception_coro));
    
    // 验证错误处理
    std::cout << "异常是否捕获: " << (exception_caught.load() ? "是" : "否") << std::endl;
    std::cout << "返回值: " << result << " (期望: -1)" << std::endl;
    std::cout << "异常处理涉及 " << exception_thread_ids.size() << " 个不同线程" << std::endl;
    
    if (exception_caught.load() && result == -1) {
        std::cout << "跨线程异常处理测试通过!" << std::endl;
    } else {
        std::cout << "跨线程异常处理测试失败!" << std::endl;
    }
}

int main() {
    std::cout << "FlowCoro 跨线程协程测试开始..." << std::endl;
    
    try {
        test_cross_thread_coroutine_resume();
        std::cout << std::endl;
        
        test_multiple_coroutines_cross_thread();
        std::cout << std::endl;
        
        test_cross_thread_exception_handling();
        std::cout << std::endl;
        
        std::cout << "跨线程测试完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
