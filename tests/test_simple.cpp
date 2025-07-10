#include <flowcoro.hpp>
#include <iostream>
#include <atomic>

// 简单的测试框架
#define EXPECT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FAIL: " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " == " << #b << std::endl; \
    } \
} while(0)

#define EXPECT_TRUE(a) do { \
    if (!(a)) { \
        std::cerr << "FAIL: " << #a << " is false" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " is true" << std::endl; \
    } \
} while(0)

#define EXPECT_FALSE(a) do { \
    if ((a)) { \
        std::cerr << "FAIL: " << #a << " is true" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " is false" << std::endl; \
    } \
} while(0)

// 测试无锁队列
void test_lockfree_queue() {
    std::cout << "Testing lockfree queue..." << std::endl;
    
    lockfree::Queue<int> queue;
    
    // 测试基本操作
    EXPECT_TRUE(queue.empty());
    
    // 入队
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    
    EXPECT_FALSE(queue.empty());
    
    // 出队
    int value;
    EXPECT_TRUE(queue.dequeue(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_TRUE(queue.dequeue(value));
    EXPECT_EQ(value, 2);
    
    EXPECT_TRUE(queue.dequeue(value));
    EXPECT_EQ(value, 3);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.dequeue(value));
    
    std::cout << "Queue test passed!" << std::endl;
}

// 测试无锁栈
void test_lockfree_stack() {
    std::cout << "Testing lockfree stack..." << std::endl;
    
    lockfree::Stack<int> stack;
    
    // 测试基本操作
    EXPECT_TRUE(stack.empty());
    
    // 入栈
    stack.push(1);
    stack.push(2);
    stack.push(3);
    
    EXPECT_FALSE(stack.empty());
    
    // 出栈 (LIFO order)
    int value;
    EXPECT_TRUE(stack.pop(value));
    EXPECT_EQ(value, 3);
    
    EXPECT_TRUE(stack.pop(value));
    EXPECT_EQ(value, 2);
    
    EXPECT_TRUE(stack.pop(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_TRUE(stack.empty());
    EXPECT_FALSE(stack.pop(value));
    
    std::cout << "Stack test passed!" << std::endl;
}

// 测试无锁环形缓冲区
void test_lockfree_ring_buffer() {
    std::cout << "Testing lockfree ring buffer..." << std::endl;
    
    lockfree::RingBuffer<int, 4> buffer;
    
    // 测试基本操作
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    
    // 填满缓冲区
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));
    
    EXPECT_FALSE(buffer.empty());
    EXPECT_TRUE(buffer.full()); // 大小为4的缓冲区，实际容量为3
    
    // 尝试再次push应该失败
    EXPECT_FALSE(buffer.push(4));
    
    // 取出元素
    int value;
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_FALSE(buffer.full());
    
    // 现在应该可以再次push
    EXPECT_TRUE(buffer.push(4));
    
    // 清空缓冲区
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 4);
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.pop(value));
    
    std::cout << "Ring buffer test passed!" << std::endl;
}

// 简单的协程测试
void test_simple_coro() {
    std::cout << "Testing simple coroutine..." << std::endl;
    
    std::atomic<bool> executed{false};
    
    auto task = [&executed]() -> flowcoro::CoroTask {
        executed.store(true, std::memory_order_release);
        co_return;
    }();
    
    // 协程在创建时不会自动执行完成
    EXPECT_FALSE(executed.load(std::memory_order_acquire));
    
    // 不使用复杂的调度器，直接运行
    if (task.handle && !task.handle.done()) {
        task.handle.resume();
    }
    
    // 协程执行完成后应该标记为已完成
    EXPECT_TRUE(task.done());
    EXPECT_TRUE(executed.load(std::memory_order_acquire));
    
    std::cout << "Simple coroutine test passed!" << std::endl;
}

int main() {
    std::cout << "Running simplified FlowCoro lockfree tests..." << std::endl;
    
    try {
        // 先测试无锁数据结构
        test_lockfree_queue();
        test_lockfree_stack();
        test_lockfree_ring_buffer();
        
        // 再测试简单协程
        test_simple_coro();
        
        std::cout << "All simplified tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}
