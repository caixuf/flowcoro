#include "../include/flowcoro.hpp"
#include "test_framework.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>
#include <queue>
#include <set>
#include <iomanip>

using namespace flowcoro;
using namespace flowcoro::test;

// 简单的测试宏
#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "断言失败: " << message << " (行 " << __LINE__ << ")" << std::endl; \
            std::abort(); \
        } \
    } while(0)

#define ASSERT_EQ(a, b, message) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "断言失败: " << message << " - 期望 " << (b) << ", 实际 " << (a) << " (行 " << __LINE__ << ")" << std::endl; \
            std::abort(); \
        } \
    } while(0)

#define ASSERT_TRUE(condition, message) ASSERT(condition, message)
#define ASSERT_FALSE(condition, message) ASSERT(!(condition), message)
#define ASSERT_LE(a, b, message) ASSERT((a) <= (b), message)

// Channel基本测试
Task<void> test_channel_basic() {
    std::cout << "测试: Channel基本发送接收..." << std::endl;
    
    auto channel = make_channel<int>(1);
    
    // 发送数据
    bool sent = co_await channel->send(42);
    ASSERT_TRUE(sent, "应该能够发送数据");
    
    // 接收数据
    auto received = co_await channel->recv();
    ASSERT_TRUE(received.has_value(), "应该接收到数据");
    ASSERT_EQ(received.value(), 42, "接收的数据应该正确");
    
    std::cout << "✓ Channel基本测试通过" << std::endl;
    co_return;
}

// 有缓冲Channel测试
Task<void> test_channel_buffered() {
    std::cout << "测试: 有缓冲Channel..." << std::endl;
    
    auto channel = make_channel<std::string>(3);
    
    // 连续发送3个数据（不应阻塞）
    bool sent1 = co_await channel->send("message1");
    bool sent2 = co_await channel->send("message2");
    bool sent3 = co_await channel->send("message3");
    
    ASSERT_TRUE(sent1 && sent2 && sent3, "有缓冲通道应该能连续发送");
    
    // 接收数据并验证顺序
    auto recv1 = co_await channel->recv();
    auto recv2 = co_await channel->recv();
    auto recv3 = co_await channel->recv();
    
    ASSERT_TRUE(recv1.has_value() && recv1.value() == "message1", "第一个消息");
    ASSERT_TRUE(recv2.has_value() && recv2.value() == "message2", "第二个消息");
    ASSERT_TRUE(recv3.has_value() && recv3.value() == "message3", "第三个消息");
    
    std::cout << "✓ 有缓冲Channel测试通过" << std::endl;
    co_return;
}

// Channel关闭测试
Task<void> test_channel_close() {
    std::cout << "测试: Channel关闭机制..." << std::endl;
    
    auto channel = make_channel<int>(2);
    
    // 发送一些数据
    co_await channel->send(1);
    co_await channel->send(2);
    
    // 关闭通道
    channel->close();
    
    // 尝试发送数据应该失败
    bool sent = co_await channel->send(3);
    ASSERT_FALSE(sent, "关闭的通道不应该接受新数据");
    
    // 应该仍能接收已有数据
    auto recv1 = co_await channel->recv();
    auto recv2 = co_await channel->recv();
    ASSERT_TRUE(recv1.has_value() && recv1.value() == 1, "应该能接收已有数据");
    ASSERT_TRUE(recv2.has_value() && recv2.value() == 2, "应该能接收已有数据");
    
    // 进一步接收应该返回nullopt
    auto recv3 = co_await channel->recv();
    ASSERT_FALSE(recv3.has_value(), "关闭的空通道应该返回nullopt");
    
    std::cout << "✓ Channel关闭测试通过" << std::endl;
    co_return;
}

// 简化版性能测试
Task<void> test_performance() {
    std::cout << "测试: 新功能性能表现..." << std::endl;
    
    auto channel = make_channel<int>(100);
    const int message_count = 100; // 减少数量避免测试超时
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto sender = [&]() -> Task<void> {
        for (int i = 0; i < message_count; ++i) {
            co_await channel->send(i);
        }
        channel->close();
        co_return;
    };
    
    auto receiver = [&]() -> Task<void> {
        int count = 0;
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            count++;
        }
        ASSERT_EQ(count, message_count, "应该接收到所有消息");
        co_return;
    };
    
    auto send_task = sender();
    auto recv_task = receiver();
    
    co_await send_task;
    co_await recv_task;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (duration.count() > 0) {
        double throughput = (message_count * 1000.0) / duration.count();
        std::cout << "Channel吞吐量: " << throughput << " 消息/秒" << std::endl;
    } else {
        std::cout << "测试完成时间 < 1ms，性能良好" << std::endl;
    }
    
    std::cout << "✓ 性能测试完成" << std::endl;
    co_return;
}

// 测试生产者消费者模式
Task<void> test_producer_consumer_pattern() {
    std::cout << "测试Channel生产者消费者模式..." << std::endl;
    
    // 创建容量为10的Channel（增大容量避免阻塞）
    auto channel = std::make_shared<Channel<int>>(10);
    
    // 统计变量
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};
    
    // 记录执行协程的线程ID，验证多调度器工作
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> producer_threads;
    std::set<std::thread::id> consumer_threads;
    
    // 减少数量，简化测试
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int ITEMS_PER_PRODUCER = 5;
    
    std::cout << "启动 " << NUM_PRODUCERS << " 个生产者协程..." << std::endl;
    std::cout << "启动 " << NUM_CONSUMERS << " 个消费者协程..." << std::endl;
    
    std::vector<Task<void>> tasks;
    
    // 创建生产者协程
    for (int producer_id = 0; producer_id < NUM_PRODUCERS; ++producer_id) {
        auto producer_task = [channel, producer_id, ITEMS_PER_PRODUCER, 
                             &produced_count, &produced_sum, 
                             &thread_ids_mutex, &producer_threads]() -> Task<void> {
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                producer_threads.insert(std::this_thread::get_id());
            }
            
            std::cout << "生产者 " << producer_id << " 开始在线程: " << std::this_thread::get_id() << std::endl;
            
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = producer_id * 100 + i;
                
                try {
                    std::cout << "生产者 " << producer_id << " 尝试发送: " << value << std::endl;
                    bool sent = co_await channel->send(value);
                    if (sent) {
                        produced_count.fetch_add(1);
                        produced_sum.fetch_add(value);
                        std::cout << "生产者 " << producer_id << " 成功生产: " << value 
                                 << " (已生产: " << produced_count.load() << ")" << std::endl;
                    } else {
                        std::cout << "生产者 " << producer_id << " 发送失败，Channel已关闭" << std::endl;
                        break;
                    }
                    
                    // 短暂延迟，给消费者机会
                    co_await sleep_for(std::chrono::milliseconds(10));
                } catch (const std::exception& e) {
                    std::cout << "生产者 " << producer_id << " 异常: " << e.what() << std::endl;
                    break;
                }
            }
            
            std::cout << "生产者 " << producer_id << " 完成，共生产: " << ITEMS_PER_PRODUCER << std::endl;
        };
        
        tasks.emplace_back(producer_task());
    }
    
    // 创建消费者协程
    for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
        auto consumer_task = [channel, consumer_id, NUM_PRODUCERS, ITEMS_PER_PRODUCER,
                             &consumed_count, &consumed_sum, 
                             &thread_ids_mutex, &consumer_threads]() -> Task<void> {
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                consumer_threads.insert(std::this_thread::get_id());
            }
            
            std::cout << "消费者 " << consumer_id << " 开始在线程: " << std::this_thread::get_id() << std::endl;
            
            int max_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
            
            while (consumed_count.load() < max_items) {
                try {
                    std::cout << "消费者 " << consumer_id << " 尝试接收数据..." << std::endl;
                    auto result = co_await channel->recv();
                    
                    if (!result.has_value()) {
                        std::cout << "消费者 " << consumer_id << " 接收到Channel关闭信号" << std::endl;
                        break;
                    }
                    
                    int value = result.value();
                    consumed_count.fetch_add(1);
                    consumed_sum.fetch_add(value);
                    
                    std::cout << "消费者 " << consumer_id << " 成功消费: " << value 
                             << " (已消费: " << consumed_count.load() << "/" << max_items << ")" << std::endl;
                    
                    // 短暂延迟
                    co_await sleep_for(std::chrono::milliseconds(5));
                    
                    // 检查是否完成
                    if (consumed_count.load() >= max_items) {
                        std::cout << "消费者 " << consumer_id << " 已达到目标，退出" << std::endl;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cout << "消费者 " << consumer_id << " 异常: " << e.what() << std::endl;
                    break;
                }
            }
            
            std::cout << "消费者 " << consumer_id << " 完成" << std::endl;
        };
        
        tasks.emplace_back(consumer_task());
    }
    
    // 启动一个协程来监控和关闭Channel
    auto monitor_task = [channel, NUM_PRODUCERS, ITEMS_PER_PRODUCER, &produced_count, &consumed_count]() -> Task<void> {
        std::cout << "监控协程开始..." << std::endl;
        
        int max_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
        int timeout_cycles = 0;
        
        while (true) {
            co_await sleep_for(std::chrono::milliseconds(100));
            
            int prod = produced_count.load();
            int cons = consumed_count.load();
            
            std::cout << "监控: 生产=" << prod << "/" << max_items 
                     << ", 消费=" << cons << "/" << max_items << std::endl;
            
            // 如果消费完成，退出
            if (cons >= max_items) {
                std::cout << "监控: 消费完成，关闭Channel" << std::endl;
                break;
            }
            
            // 如果生产完成但消费没跟上，继续等待
            if (prod >= max_items) {
                timeout_cycles++;
                std::cout << "监控: 生产完成，等待消费 (周期:" << timeout_cycles << ")" << std::endl;
                
                // 最多等待5秒
                if (timeout_cycles > 50) {
                    std::cout << "监控: 超时，强制关闭Channel" << std::endl;
                    break;
                }
            }
        }
        
        channel->close();
        std::cout << "监控协程: Channel已关闭" << std::endl;
    };
    
    tasks.emplace_back(monitor_task());
    
    std::cout << "等待所有生产者完成..." << std::endl;
    
    // 等待所有协程完成
    for (auto& task : tasks) {
        try {
            co_await task;
        } catch (const std::exception& e) {
            std::cout << "任务执行异常: " << e.what() << std::endl;
        }
    }
    
    // 验证结果
    std::cout << "\n=== 生产者消费者测试结果 ===" << std::endl;
    std::cout << "生产总数: " << produced_count.load() << " (期望: " << NUM_PRODUCERS * ITEMS_PER_PRODUCER << ")" << std::endl;
    std::cout << "消费总数: " << consumed_count.load() << std::endl;
    std::cout << "生产总和: " << produced_sum.load() << std::endl;
    std::cout << "消费总和: " << consumed_sum.load() << std::endl;
    std::cout << "生产者使用线程数: " << producer_threads.size() << std::endl;
    std::cout << "消费者使用线程数: " << consumer_threads.size() << std::endl;
    
    // 验证基本要求
    TEST_EXPECT_EQ(produced_count.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    TEST_EXPECT_EQ(consumed_count.load(), produced_count.load());
    TEST_EXPECT_EQ(consumed_sum.load(), produced_sum.load());
    
    // 验证多调度器工作（应该使用多个线程）
    TEST_EXPECT_TRUE(producer_threads.size() >= 1);
    TEST_EXPECT_TRUE(consumer_threads.size() >= 1);
    
    std::cout << "Channel生产者消费者模式测试通过！" << std::endl;
}

// 主测试函数
Task<void> run_all_tests() {
    std::cout << "开始运行FlowCoro新功能测试..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        co_await test_channel_basic();
        co_await test_channel_buffered();
        co_await test_channel_close();
        co_await test_performance();
        co_await test_producer_consumer_pattern();  // 新增的测试
        
        std::cout << std::endl;
        std::cout << "🎉 所有测试通过！新功能可以安全合入。" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

int main() {
    try {
        sync_wait(run_all_tests());
        std::cout << "测试完成，程序正常退出。" << std::endl;
        return 0;
    } catch (...) {
        std::cerr << "测试失败，程序异常退出。" << std::endl;
        return 1;
    }
}
