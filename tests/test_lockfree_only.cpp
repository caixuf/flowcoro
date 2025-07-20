#include "../include/flowcoro/lockfree.h"
#include "test_framework.h"
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <memory>

using namespace flowcoro::test;

// 测试lockfree::Queue的基本功能
void test_basic_queue() {
    std::cout << "🧪 Test Suite: Basic Queue Tests\n";
    std::cout << "========================================\n";
    
    lockfree::Queue<int> queue;
    
    // 测试基本的入队和出队
    queue.enqueue(42);
    queue.enqueue(99);
    
    int result1, result2;
    TEST_EXPECT_TRUE(queue.dequeue(result1));
    TEST_EXPECT_EQ(result1, 42);
    
    TEST_EXPECT_TRUE(queue.dequeue(result2));
    TEST_EXPECT_EQ(result2, 99);
    
    // 空队列应该返回false
    int dummy;
    TEST_EXPECT_FALSE(queue.dequeue(dummy));
    
    TestRunner::print_summary();
    TestRunner::reset();
}

// 测试队列的并发安全性
void test_concurrent_queue() {
    std::cout << "\n🧪 Test Suite: Concurrent Queue Tests\n";
    std::cout << "========================================\n";
    
    lockfree::Queue<int> queue;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    constexpr int num_items = 1000;
    
    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            queue.enqueue(i);
            produced.fetch_add(1);
        }
    });
    
    // 消费者线程
    std::thread consumer([&]() {
        int item;
        int count = 0;
        while (count < num_items) {
            if (queue.dequeue(item)) {
                count++;
                consumed.fetch_add(1);
            }
            // 短暂让出CPU
            std::this_thread::yield();
        }
    });
    
    producer.join();
    consumer.join();
    
    TEST_EXPECT_EQ(produced.load(), num_items);
    TEST_EXPECT_EQ(consumed.load(), num_items);
    
    TestRunner::print_summary();
    TestRunner::reset();
}

// 测试队列的析构安全性
void test_destruction_safety() {
    std::cout << "\n🧪 Test Suite: Destruction Safety Tests\n";
    std::cout << "========================================\n";
    
    // 创建队列并在不同的作用域中测试
    {
        lockfree::Queue<int> queue;
        queue.enqueue(1);
        queue.enqueue(2);
        queue.enqueue(3);
        
        int item;
        int count = 0;
        while (queue.dequeue(item)) {
            count++;
        }
        TEST_EXPECT_EQ(count, 3);
    } // 队列在这里被析构
    
    // 测试多线程场景下的析构安全性
    std::atomic<bool> continue_test{true};
    
    auto queue_ptr = std::make_unique<lockfree::Queue<int>>();
    
    // 启动一个线程不断尝试操作队列
    std::thread worker([&]() {
        int item = 0;
        while (continue_test.load()) {
            queue_ptr->enqueue(item++);
            int dummy;
            queue_ptr->dequeue(dummy);
            std::this_thread::yield();
        }
    });
    
    // 让工作线程运行一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 标记停止并析构队列
    continue_test.store(false);
    queue_ptr.reset(); // 析构队列
    
    worker.join();
    
    TEST_EXPECT_TRUE(true); // 如果能到这里说明没有崩溃
    
    TestRunner::print_summary();
    TestRunner::reset();
}

int main() {
    std::cout << "🧪 FlowCoro lockfree::Queue Tests\n";
    std::cout << "=================================\n\n";
    
    test_basic_queue();
    test_concurrent_queue();
    test_destruction_safety();
    
    std::cout << "✅ All lockfree::Queue tests completed!\n";
    return 0;
}
