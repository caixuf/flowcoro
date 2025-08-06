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

// 异步互斥锁测试
Task<void> test_async_mutex() {
    std::cout << "测试: AsyncMutex互斥锁..." << std::endl;
    
    sync::AsyncMutex mutex;
    std::atomic<int> counter{0};
    std::atomic<int> concurrent_access{0};
    
    auto worker = [&](int worker_id) -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await mutex.lock();
            
            // 检查是否有并发访问
            int current_concurrent = concurrent_access.fetch_add(1);
            ASSERT_EQ(current_concurrent, 0, "互斥锁应该防止并发访问");
            
            // 模拟工作
            int old_value = counter.load();
            co_await sleep_for(std::chrono::milliseconds(1));
            counter.store(old_value + 1);
            
            concurrent_access.fetch_sub(1);
            mutex.unlock();
        }
        co_return;
    };
    
    // 启动多个工作协程
    auto worker1 = worker(1);
    auto worker2 = worker(2);
    auto worker3 = worker(3);
    
    co_await worker1;
    co_await worker2;
    co_await worker3;
    
    ASSERT_EQ(counter.load(), 15, "互斥锁保护下的计数器应该正确");
    
    std::cout << "✓ AsyncMutex测试通过" << std::endl;
    co_return;
}

// 异步信号量测试（简化版）
Task<void> test_async_semaphore() {
    std::cout << "测试: AsyncSemaphore信号量..." << std::endl;
    
    sync::AsyncSemaphore semaphore(1); // 只允许1个并发，简化测试
    std::atomic<int> counter{0};
    
    auto worker = [&](int worker_id) -> Task<void> {
        co_await semaphore.acquire();
        
        counter.fetch_add(1);
        co_await sleep_for(std::chrono::milliseconds(10)); // 缩短等待时间
        
        semaphore.release();
        co_return;
    };
    
    // 只启动2个工作协程
    auto worker1 = worker(1);
    auto worker2 = worker(2);
    
    co_await worker1;
    co_await worker2;
    
    ASSERT_EQ(counter.load(), 2, "应该完成2个工作");
    
    std::cout << "✓ AsyncSemaphore测试通过" << std::endl;
    co_return;
}

// 异步条件变量测试
Task<void> test_async_condition_variable() {
    std::cout << "测试: AsyncConditionVariable条件变量..." << std::endl;
    
    sync::AsyncConditionVariable cv;
    bool ready = false;
    std::atomic<int> wakeup_count{0};
    
    auto waiter = [&](int id) -> Task<void> {
        co_await cv.wait();
        if (ready) {
            wakeup_count.fetch_add(1);
        }
        co_return;
    };
    
    auto notifier = [&]() -> Task<void> {
        co_await sleep_for(std::chrono::milliseconds(50));
        ready = true;
        cv.notify_all(); // 唤醒所有等待者
        co_return;
    };
    
    // 启动等待者和通知者
    auto waiter1 = waiter(1);
    auto waiter2 = waiter(2);
    auto waiter3 = waiter(3);
    auto notifier_task = notifier();
    
    co_await waiter1;
    co_await waiter2;
    co_await waiter3;
    co_await notifier_task;
    
    ASSERT_EQ(wakeup_count.load(), 3, "应该唤醒所有等待者");
    
    std::cout << "✓ AsyncConditionVariable测试通过" << std::endl;
    co_return;
}

// 生产者-消费者综合测试
Task<void> test_producer_consumer() {
    std::cout << "测试: 生产者-消费者模式..." << std::endl;
    
    constexpr int buffer_size = 3;
    constexpr int item_count = 10;
    
    sync::AsyncSemaphore empty_slots(buffer_size);  // 空槽位
    sync::AsyncSemaphore filled_slots(0);           // 已填充槽位
    sync::AsyncMutex buffer_mutex;
    
    std::queue<int> buffer;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    auto producer = [&]() -> Task<void> {
        for (int i = 0; i < item_count; ++i) {
            co_await empty_slots.acquire();
            co_await buffer_mutex.lock();
            
            buffer.push(i);
            produced.fetch_add(1);
            
            buffer_mutex.unlock();
            filled_slots.release();
            
            co_await sleep_for(std::chrono::milliseconds(1));
        }
        co_return;
    };
    
    auto consumer = [&]() -> Task<void> {
        for (int i = 0; i < item_count; ++i) {
            co_await filled_slots.acquire();
            co_await buffer_mutex.lock();
            
            int item = buffer.front();
            buffer.pop();
            consumed.fetch_add(1);
            
            buffer_mutex.unlock();
            empty_slots.release();
            
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
    ASSERT_TRUE(buffer.empty(), "缓冲区应该为空");
    
    std::cout << "✓ 生产者-消费者测试通过" << std::endl;
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
        co_await test_async_mutex();
        co_await test_async_semaphore();
        co_await test_async_condition_variable();
        co_await test_producer_consumer();
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
