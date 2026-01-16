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

// 
TEST_CASE(basic_coroutine) {
    bool executed = false;

    auto coro = [&]() -> Task<int> {
        executed = true;
        co_return 42;
    }();

    // 
    auto result = sync_wait(std::move(coro));
    TEST_EXPECT_EQ(result, 42);
    
    // 
    TEST_EXPECT_TRUE(executed);
}

// 
TEST_CASE(thread_pool) {
    ThreadPool pool(4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    // 
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.enqueue([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }));
    }

    // 
    for (auto& f : futures) {
        f.wait();
    }

    TEST_EXPECT_EQ(counter.load(), 10);
}

//  - 
TEST_CASE(memory_pool) {
    MemoryPool pool(64, 16); // 6416

    // 
    std::vector<void*> ptrs;
    for (int i = 0; i < 16; ++i) {
        void* ptr = pool.allocate();
        TEST_EXPECT_TRUE(ptr != nullptr);
        ptrs.push_back(ptr);
    }

    // 
    void* ptr = pool.allocate();
    TEST_EXPECT_TRUE(ptr != nullptr); // 
    ptrs.push_back(ptr);

    // 
    for (size_t i = 0; i < 8; ++i) {
        pool.deallocate(ptrs[i]);
    }

    // 
    ptr = pool.allocate();
    TEST_EXPECT_TRUE(ptr != nullptr);
    pool.deallocate(ptr);
}

// 
TEST_CASE(object_pool) {
    struct TestObject {
        int value = 0;
        TestObject() = default;
        TestObject(int v) : value(v) {}
    };

    ObjectPool<TestObject> pool(8);

    // 
    auto obj1 = pool.acquire();
    obj1->value = 42;
    TEST_EXPECT_EQ(obj1->value, 42);

    auto obj2 = pool.acquire();
    obj2->value = 100;
    TEST_EXPECT_EQ(obj2->value, 100);

    // 
    pool.release(std::move(obj1));
    pool.release(std::move(obj2));

    // 
    auto obj3 = pool.acquire();
    TEST_EXPECT_TRUE(obj3 != nullptr);
}

// 
TEST_CASE(lockfree_queue) {
    Queue<int> queue;

    // 
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

    // 
    TEST_EXPECT_FALSE(queue.dequeue(value));
}

//  - 
TEST_CASE(lockfree_queue_multithreaded) {
    Queue<int> queue;
    const int num_items = 100; // 

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> production_done{false};

    // 
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
            // 
        }
        co_return;
    };

    // 
    auto prod = producer_task();
    auto cons = consumer_task();

    // 
    auto& manager = CoroutineManager::get_instance();
    schedule_coroutine_enhanced(prod.handle);
    schedule_coroutine_enhanced(cons.handle);

    // 
    int timeout_counter = 0;
    while ((!prod.handle.done() || !cons.handle.done()) && timeout_counter < 1000) {
        manager.drive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timeout_counter++;
    }

    TEST_EXPECT_EQ(produced.load(), num_items);
    TEST_EXPECT_EQ(consumed.load(), num_items);
}

// 
TEST_CASE(coroutine_sync_wait) {
    std::atomic<int> counter{0};

    // 
    auto task = [&counter]() -> Task<int> {
        counter.fetch_add(1);
        co_return 42;
    }();

    // sync_wait
    auto result = sync_wait(std::move(task));
    
    TEST_EXPECT_EQ(counter.load(), 1);
    TEST_EXPECT_EQ(result, 42);
}

int main() {
    TEST_SUITE("FlowCoro Core Functionality Tests");

    TestRunner::print_summary();

    return TestRunner::all_passed() ? 0 : 1;
}
