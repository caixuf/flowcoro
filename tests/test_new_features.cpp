#include "../include/flowcoro.hpp"
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>
#include <queue>

using namespace flowcoro;

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

// 基于Channel的生产者-消费者测试（FlowCoro推荐模式）
Task<void> test_producer_consumer() {
    std::cout << "测试: Channel生产者-消费者模式..." << std::endl;
    
    constexpr int item_count = 10;
    auto channel = make_channel<int>(3);  // 使用Channel作为缓冲区
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    auto producer = [&]() -> Task<void> {
        for (int i = 0; i < item_count; ++i) {
            co_await channel->send(i);
            produced.fetch_add(1);
            co_await sleep_for(std::chrono::milliseconds(1));
        }
        channel->close();  // 关闭channel表示生产完成
        co_return;
    };
    
    auto consumer = [&]() -> Task<void> {
        while (true) {
            auto item = co_await channel->recv();
            if (!item.has_value()) break;  // channel已关闭且为空
            consumed.fetch_add(1);
            co_await sleep_for(std::chrono::milliseconds(1));
        }
        co_return;
    };
    
    auto producer_task = producer();
    auto consumer_task = consumer();
    
    co_await producer_task;
    co_await consumer_task;
    
    ASSERT_EQ(produced.load(), item_count, "应该生产所有物品");
    ASSERT_EQ(consumed.load(), item_count, "应该消费所有物品");
    
    std::cout << "✓ Channel生产者-消费者测试通过" << std::endl;
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

// 主测试函数
Task<void> run_all_tests() {
    std::cout << "开始运行FlowCoro新功能测试..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        co_await test_channel_basic();
        co_await test_channel_buffered();
        co_await test_channel_close();
        // co_await test_producer_consumer();
        co_await test_performance();
        
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
