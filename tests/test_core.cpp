#include <iostream>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

#include "../include/flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace flowcoro::lockfree;

// 测试协程基础功能
TEST_CASE(basic_coroutine) {
    bool executed = false;
    
    auto coro = [&]() -> Task<int> {
        executed = true;
        co_return 42;
    }();
    
    // 协程应该已经开始执行
    TEST_EXPECT_TRUE(executed);
    
    // 等待协程完成
    auto result = sync_wait(std::move(coro));
    TEST_EXPECT_EQ(result, 42);
}

// 测试线程池
TEST_CASE(thread_pool) {
    ThreadPool pool(4);
    
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    
    // 提交多个任务
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }));
    }
    
    // 等待所有任务完成
    for (auto& f : futures) {
        f.wait();
    }
    
    TEST_EXPECT_EQ(counter.load(), 10);
}

// 测试内存池
TEST_CASE(memory_pool) {
    MemoryPool pool(64, 16); // 64字节块，16个块
    
    // 分配内存
    std::vector<void*> ptrs;
    for (int i = 0; i < 16; ++i) {
        void* ptr = pool.allocate();
        TEST_EXPECT_TRUE(ptr != nullptr);
        ptrs.push_back(ptr);
    }
    
    // 应该已经用完
    void* ptr = pool.allocate();
    TEST_EXPECT_TRUE(ptr == nullptr); // 池已满
    
    // 释放一些内存
    for (size_t i = 0; i < 8; ++i) {
        pool.deallocate(ptrs[i]);
    }
    
    // 现在应该能再分配
    ptr = pool.allocate();
    TEST_EXPECT_TRUE(ptr != nullptr);
    pool.deallocate(ptr);
}

// 测试对象池
TEST_CASE(object_pool) {
    struct TestObject {
        int value = 0;
        TestObject() = default;
        TestObject(int v) : value(v) {}
    };
    
    ObjectPool<TestObject> pool(8);
    
    // 获取对象
    auto obj1 = pool.acquire();
    obj1->value = 42;
    TEST_EXPECT_EQ(obj1->value, 42);
    
    auto obj2 = pool.acquire();
    obj2->value = 100;
    TEST_EXPECT_EQ(obj2->value, 100);
    
    // 归还对象
    pool.release(std::move(obj1));
    pool.release(std::move(obj2));
    
    // 再次获取（可能是复用的对象）
    auto obj3 = pool.acquire();
    TEST_EXPECT_TRUE(obj3 != nullptr);
}

// 测试无锁队列
TEST_CASE(lockfree_queue) {
    LockFreeQueue<int> queue;
    
    // 测试单线程操作
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    int value;
    TEST_EXPECT_TRUE(queue.try_pop(value));
    TEST_EXPECT_EQ(value, 1);
    
    TEST_EXPECT_TRUE(queue.try_pop(value));
    TEST_EXPECT_EQ(value, 2);
    
    TEST_EXPECT_TRUE(queue.try_pop(value));
    TEST_EXPECT_EQ(value, 3);
    
    // 队列应该为空
    TEST_EXPECT_FALSE(queue.try_pop(value));
}

// 测试多线程下的无锁队列
TEST_CASE(lockfree_queue_multithreaded) {
    LockFreeQueue<int> queue;
    const int num_items = 1000;
    const int num_producers = 4;
    const int num_consumers = 4;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // 启动生产者
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&queue, &produced, num_items, i]() {
            int items_per_producer = num_items / 4;
            for (int j = 0; j < items_per_producer; ++j) {
                queue.push(i * items_per_producer + j);
                produced.fetch_add(1);
            }
        });
    }
    
    // 启动消费者
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&queue, &consumed]() {
            int value;
            while (true) {
                if (queue.try_pop(value)) {
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    // 如果生产已完成且队列为空，退出
                    if (consumed.load() >= 1000) break;
                }
            }
        });
    }
    
    // 等待生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    // 等待消费者完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (auto& t : consumers) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    TEST_EXPECT_EQ(produced.load(), num_items);
    TEST_EXPECT_TRUE(consumed.load() <= num_items);
}

// 测试协程与线程池的结合
TEST_CASE(coroutine_with_threadpool) {
    ThreadPool pool(2);
    
    auto compute_task = [&pool](int n) -> Task<int> {
        // 在线程池中执行计算
        co_return co_await pool.schedule([n]() {
            int result = 0;
            for (int i = 0; i <= n; ++i) {
                result += i;
            }
            return result;
        });
    };
    
    auto task1 = compute_task(100);  // 1+2+...+100 = 5050
    auto task2 = compute_task(50);   // 1+2+...+50 = 1275
    
    auto result1 = sync_wait(std::move(task1));
    auto result2 = sync_wait(std::move(task2));
    
    TEST_EXPECT_EQ(result1, 5050);
    TEST_EXPECT_EQ(result2, 1275);
}

int main() {
    TEST_SUITE("FlowCoro Core Functionality Tests");
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
