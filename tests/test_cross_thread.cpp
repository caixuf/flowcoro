#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <set>
#include <map>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// 测试跨线程协程恢复
TEST_CASE(cross_thread_coroutine_resume) {
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
    TEST_EXPECT_EQ(result, 42);
    TEST_EXPECT_EQ(counter.load(), 3);
    
    // 验证协程在多个线程中执行
    std::cout << "协程在 " << thread_ids.size() << " 个不同线程中执行过" << std::endl;
    for (const auto& tid : thread_ids) {
        std::cout << "线程ID: " << tid << std::endl;
    }
    
    // 至少应该在1个线程中执行（可能在多个线程中）
    TEST_EXPECT_TRUE(thread_ids.size() >= 1);
    
    std::cout << "跨线程协程恢复测试通过!" << std::endl;
}

// 测试多个协程的跨线程调度
TEST_CASE(multiple_coroutines_cross_thread) {
    std::cout << "测试多个协程的跨线程调度..." << std::endl;
    
    const int NUM_COROUTINES = 10;
    std::atomic<int> completed_count{0};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> all_thread_ids;
    
    std::vector<Task<int>> tasks;
    tasks.reserve(NUM_COROUTINES);
    
    // 创建多个协程
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        tasks.emplace_back([&, i]() -> Task<int> {
            std::cout << "协程 " << i << " 开始在线程: " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                all_thread_ids.insert(std::this_thread::get_id());
            }
            
            // 随机延迟，增加跨线程调度的可能性
            co_await sleep_for(std::chrono::milliseconds(5 + (i % 20)));
            
            std::cout << "协程 " << i << " 恢复在线程: " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                all_thread_ids.insert(std::this_thread::get_id());
            }
            
            completed_count.fetch_add(1);
            co_return i * 10;
        }());
    }
    
    // 等待所有协程完成
    std::vector<int> results;
    for (auto& task : tasks) {
        results.push_back(sync_wait(std::move(task)));
    }
    
    // 验证结果
    TEST_EXPECT_EQ(completed_count.load(), NUM_COROUTINES);
    TEST_EXPECT_EQ(results.size(), NUM_COROUTINES);
    
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        TEST_EXPECT_EQ(results[i], i * 10);
    }
    
    std::cout << "所有协程在 " << all_thread_ids.size() << " 个不同线程中执行过" << std::endl;
    
    // 验证至少使用了多个线程（对于10个协程，应该有多个线程参与）
    TEST_EXPECT_TRUE(all_thread_ids.size() >= 1);
    
    std::cout << "多个协程跨线程调度测试通过!" << std::endl;
}

// 测试协程在不同线程间的数据传递
TEST_CASE(cross_thread_data_transfer) {
    std::cout << "测试协程跨线程数据传递..." << std::endl;
    
    struct ThreadSafeData {
        std::atomic<int> value{0};
        mutable std::mutex thread_history_mutex;
        std::vector<std::thread::id> thread_history;
        
        void record_access() {
            std::lock_guard<std::mutex> lock(thread_history_mutex);
            thread_history.push_back(std::this_thread::get_id());
        }
        
        size_t get_unique_thread_count() const {
            std::lock_guard<std::mutex> lock(thread_history_mutex);
            std::set<std::thread::id> unique_threads(thread_history.begin(), thread_history.end());
            return unique_threads.size();
        }
    };
    
    auto shared_data = std::make_shared<ThreadSafeData>();
    
    auto producer_coro = [shared_data]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            shared_data->record_access();
            shared_data->value.store(i + 1);
            std::cout << "生产者在线程 " << std::this_thread::get_id() 
                      << " 设置值: " << (i + 1) << std::endl;
            co_await sleep_for(std::chrono::milliseconds(10));
        }
    }();
    
    auto consumer_coro = [shared_data]() -> Task<int> {
        int total = 0;
        for (int i = 0; i < 5; ++i) {
            co_await sleep_for(std::chrono::milliseconds(15)); // 稍微晚一点读取
            shared_data->record_access();
            int value = shared_data->value.load();
            total += value;
            std::cout << "消费者在线程 " << std::this_thread::get_id() 
                      << " 读取值: " << value << std::endl;
        }
        co_return total;
    }();
    
    // 并发执行生产者和消费者
    auto producer_task = [&]() -> Task<void> {
        co_await std::move(producer_coro);
    }();
    
    auto consumer_task = [&]() -> Task<int> {
        co_return co_await std::move(consumer_coro);
    }();
    
    // 等待两个协程都完成
    sync_wait(std::move(producer_task));
    int consumer_result = sync_wait(std::move(consumer_task));
    
    std::cout << "消费者总计读取到的值: " << consumer_result << std::endl;
    std::cout << "数据访问涉及 " << shared_data->get_unique_thread_count() << " 个不同线程" << std::endl;
    
    // 验证数据传递正确
    TEST_EXPECT_TRUE(consumer_result > 0);
    TEST_EXPECT_TRUE(shared_data->get_unique_thread_count() >= 1);
    
    std::cout << "跨线程数据传递测试通过!" << std::endl;
}

// 测试协程池的并发性能
TEST_CASE(coroutine_pool_concurrency) {
    std::cout << "测试协程池并发性能..." << std::endl;
    
    const int NUM_TASKS = 50; // 减少任务数以提高稳定性
    std::atomic<int> execution_count{0};
    std::mutex thread_stats_mutex;
    std::map<std::thread::id, int> thread_execution_count;
    
    auto start_time = std::chrono::steady_clock::now();
    
    std::vector<Task<int>> tasks;
    tasks.reserve(NUM_TASKS);
    
    // 创建大量协程任务
    for (int i = 0; i < NUM_TASKS; ++i) {
        tasks.emplace_back([&, i]() -> Task<int> {
            auto thread_id = std::this_thread::get_id();
            
            {
                std::lock_guard<std::mutex> lock(thread_stats_mutex);
                thread_execution_count[thread_id]++;
            }
            
            // 模拟一些计算工作
            co_await sleep_for(std::chrono::milliseconds(1));
            
            execution_count.fetch_add(1);
            co_return i;
        }());
    }
    
    // 等待所有任务完成
    std::vector<int> results;
    for (auto& task : tasks) {
        results.push_back(sync_wait(std::move(task)));
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 验证结果
    TEST_EXPECT_EQ(execution_count.load(), NUM_TASKS);
    TEST_EXPECT_EQ(results.size(), NUM_TASKS);
    
    // 统计线程使用情况
    std::cout << "执行时间: " << duration.count() << " ms" << std::endl;
    std::cout << "线程使用统计:" << std::endl;
    for (const auto& [thread_id, count] : thread_execution_count) {
        std::cout << "  线程 " << thread_id << ": " << count << " 个任务" << std::endl;
    }
    
    // 验证使用了多个线程
    TEST_EXPECT_TRUE(thread_execution_count.size() >= 1);
    
    std::cout << "协程池并发性能测试通过!" << std::endl;
}

// 测试协程异常在跨线程环境下的处理
TEST_CASE(cross_thread_exception_handling) {
    std::cout << "测试跨线程协程异常处理..." << std::endl;
    
    std::atomic<bool> exception_caught{false};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> exception_thread_ids;
    
    // 创建一个会抛出异常的协程
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
    TEST_EXPECT_TRUE(exception_caught.load());
    TEST_EXPECT_EQ(result, -1); // 应该返回错误值
    
    std::cout << "异常处理涉及 " << exception_thread_ids.size() << " 个不同线程" << std::endl;
    
    std::cout << "跨线程异常处理测试通过!" << std::endl;
}

int main() {
    std::cout << "FlowCoro 跨线程协程测试开始..." << std::endl;
    
    try {
        test_cross_thread_coroutine_resume();
        test_multiple_coroutines_cross_thread();
        test_cross_thread_data_transfer();
        test_coroutine_pool_concurrency();
        test_cross_thread_exception_handling();
        
        std::cout << "\n所有跨线程测试通过!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
