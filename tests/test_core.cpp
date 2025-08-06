#include <iostream>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace lockfree;

// 测试协程基础功能
TEST_CASE(basic_coroutine) {
    bool executed = false;

    auto coro = [&]() -> Task<int> {
        executed = true;
        co_return 42;
    }();

    // 等待协程完成并验证结果
    auto result = sync_wait(std::move(coro));
    TEST_EXPECT_EQ(result, 42);
    
    // 协程完成后应该已经执行过
    TEST_EXPECT_TRUE(executed);
}

// 测试线程池
TEST_CASE(thread_pool) {
    ThreadPool pool(4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    // 提交多个任务
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.enqueue([&counter]() {
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

// 测试内存池 - 支持动态扩容
TEST_CASE(memory_pool) {
    MemoryPool pool(64, 16); // 64字节块，16个块

    // 分配内存
    std::vector<void*> ptrs;
    for (int i = 0; i < 16; ++i) {
        void* ptr = pool.allocate();
        TEST_EXPECT_TRUE(ptr != nullptr);
        ptrs.push_back(ptr);
    }

    // 库支持动态扩容，继续分配应该成功
    void* ptr = pool.allocate();
    TEST_EXPECT_TRUE(ptr != nullptr); // 动态扩容
    ptrs.push_back(ptr);

    // 释放一些内存
    for (size_t i = 0; i < 8; ++i) {
        pool.deallocate(ptrs[i]);
    }

    // 应该能继续分配
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
    Queue<int> queue;

    // 测试单线程操作
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);

    int value;
    TEST_EXPECT_TRUE(queue.dequeue(value));
    TEST_EXPECT_EQ(value, 1);

    TEST_EXPECT_TRUE(queue.dequeue(value));
    TEST_EXPECT_EQ(value, 2);

    TEST_EXPECT_TRUE(queue.dequeue(value));
    TEST_EXPECT_EQ(value, 3);

    // 队列应该为空
    TEST_EXPECT_FALSE(queue.dequeue(value));
}

// 测试多线程下的无锁队列 - 使用协程池而非用户线程
TEST_CASE(lockfree_queue_multithreaded) {
    Queue<int> queue;
    const int num_items = 100; // 减少数量，便于测试

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> production_done{false};

    // 使用协程池调度任务，而不是用户创建线程
    auto producer_task = [&]() -> Task<void> {
        for (int i = 0; i < num_items; ++i) {
            queue.enqueue(i);
            produced.fetch_add(1);
        }
        production_done.store(true);
        co_return;
    };

    auto consumer_task = [&]() -> Task<void> {
        int value;
        while (consumed.load() < num_items) {
            if (queue.dequeue(value)) {
                consumed.fetch_add(1);
            }
            // 简单的忙等待，避免复杂的协程睡眠
        }
        co_return;
    };

    // 启动生产者和消费者协程
    auto prod = producer_task();
    auto cons = consumer_task();

    // 使用协程管理器调度
    auto& manager = CoroutineManager::get_instance();
    schedule_coroutine_enhanced(prod.handle);
    schedule_coroutine_enhanced(cons.handle);

    // 驱动协程执行，设置超时防止死锁
    int timeout_counter = 0;
    while ((!prod.handle.done() || !cons.handle.done()) && timeout_counter < 1000) {
        manager.drive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timeout_counter++;
    }

    TEST_EXPECT_EQ(produced.load(), num_items);
    TEST_EXPECT_EQ(consumed.load(), num_items);
}

// 测试协程池功能
TEST_CASE(coroutine_pool) {
    std::atomic<int> counter{0};
    std::vector<Task<void>> tasks;

    // 创建多个协程任务
    for (int i = 0; i < 10; ++i) {
        auto task = [&counter]() -> Task<void> {
            counter.fetch_add(1);
            co_return;
        }();

        // 使用协程池调度
        schedule_coroutine_enhanced(task.handle);
        tasks.push_back(std::move(task));
    }

    // 驱动协程池执行所有任务
    auto& manager = CoroutineManager::get_instance();
    int timeout_counter = 0;
    bool all_done = false;

    while (!all_done && timeout_counter < 1000) {
        manager.drive();

        // 检查所有任务是否完成
        all_done = true;
        for (const auto& task : tasks) {
            if (!task.handle.done()) {
                all_done = false;
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timeout_counter++;
    }

    TEST_EXPECT_EQ(counter.load(), 10);
}

int main() {
    TEST_SUITE("FlowCoro Core Functionality Tests");

    TestRunner::print_summary();

    return TestRunner::all_passed() ? 0 : 1;
}
