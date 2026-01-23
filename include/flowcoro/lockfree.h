#pragma once
#include <atomic>
#include <memory>
#include <type_traits>
#include "memory_pool.h"

namespace lockfree {

// 使用flowcoro内存池的便利函数
using flowcoro::pool_malloc;
using flowcoro::pool_free;

// 无锁队列实现
template<typename T>
class Queue {
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
        bool is_dummy{false}; // 标记是否为虚拟节点

        Node() = default;
        explicit Node(bool dummy) : is_dummy(dummy) {}
        ~Node() {
            T* data_ptr = data.load();
            if (data_ptr) {
                data_ptr->~T(); // 显式调用析构函数
                pool_free(data_ptr); // 使用内存池释放
            }
        }
    };

    // NodeWithData结构定义移到类级别，以便整个类都能使用
    struct NodeWithData {
        Node node;
        alignas(T) char data_storage[sizeof(T)];
    };

    alignas(64) std::atomic<Node*> head;
    alignas(64) std::atomic<Node*> tail;
    alignas(64) std::atomic<bool> destroyed{false}; // 添加析构标志

public:
    Queue() {
        // 使用NodeWithData分配虚拟节点，保持分配一致性
        NodeWithData* dummy_block = static_cast<NodeWithData*>(pool_malloc(sizeof(NodeWithData)));
        new(&dummy_block->node) Node(true); // 标记为虚拟节点
        head.store(&dummy_block->node);
        tail.store(&dummy_block->node);
        destroyed.store(false);
    }

    ~Queue() {
        // 设置析构标志
        destroyed.store(true, std::memory_order_release);

        // 先清理所有数据，避免析构时访问无效内存
        T unused_item;
        while (dequeue_unsafe(unused_item)) {
            // 清空队列
        }

        // 然后清理节点链表 - 使用NodeWithData释放
        Node* current = head.load();
        while (current) {
            Node* next = current->next.load();
            current->~Node(); // 显式调用析构函数
            
            // 计算包含Node的完整块地址并释放
            NodeWithData* block = reinterpret_cast<NodeWithData*>(
                reinterpret_cast<char*>(current) - offsetof(NodeWithData, node));
            pool_free(block); // 释放整个块
            
            current = next;
        }
    }

    void enqueue(T item) {
        if (destroyed.load(std::memory_order_acquire)) {
            return; // 队列已析构，丢弃任务
        }

        // 优化：一次分配包含Node和数据的内存块
        NodeWithData* block = static_cast<NodeWithData*>(pool_malloc(sizeof(NodeWithData)));
        new(&block->node) Node(false); // 标记为数据节点
        
        T* data_ptr = reinterpret_cast<T*>(block->data_storage);
        new(data_ptr) T(std::move(item)); // placement new for data
        block->node.data.store(data_ptr);

        Node* prev_tail = tail.exchange(&block->node);
        if (prev_tail) {
            prev_tail->next.store(&block->node);
        }
    }

    bool dequeue(T& result) {
        if (destroyed.load(std::memory_order_acquire)) {
            return false; // 队列已析构
        }

        return dequeue_unsafe(result);
    }

private:
    bool dequeue_unsafe(T& result) {
        Node* head_node = head.load();
        if (!head_node) {
            return false; // 队列已被析构
        }

        Node* next = head_node->next.load();
        if (next == nullptr) {
            return false; // 队列为空
        }

        T* data_ptr = next->data.exchange(nullptr);
        if (data_ptr == nullptr) {
            return false; // 数据已被其他线程取走
        }

        result = *data_ptr;
        data_ptr->~T(); // 显式调用析构函数

        // 尝试更新head，如果失败也不要紧，下次调用会重试
        Node* expected = head_node;
        if (head.compare_exchange_weak(expected, next)) {
            head_node->~Node(); // 显式调用析构函数
            
            // 计算包含Node的完整块地址并释放
            // 现在所有节点（包括虚拟节点）都使用NodeWithData分配，所以这是安全的
            NodeWithData* block = reinterpret_cast<NodeWithData*>(
                reinterpret_cast<char*>(head_node) - offsetof(NodeWithData, node));
            pool_free(block); // 释放整个块
        }

        return true;
    }

public:

    bool empty() const {
        Node* head_node = head.load();
        return head_node->next.load() == nullptr;
    }

    // 估算队列大小（注意：这只是估计值，可能不准确）
    size_t size_estimate() const {
        if (destroyed.load(std::memory_order_acquire)) {
            return 0;
        }
        
        size_t count = 0;
        Node* current = head.load();
        if (current) {
            current = current->next.load(); // 跳过虚拟头节点
            while (current && count < 10000) { // 限制最大计数以避免长时间循环
                current = current->next.load();
                count++;
            }
        }
        return count;
    }
};

// 无锁栈实现 (Treiber Stack) - 优化内存分配
template<typename T>
class Stack {
private:
    struct Node {
        T data;
        Node* next;

        Node(T&& item) : data(std::move(item)), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head{nullptr};

public:
    ~Stack() {
        while (Node* old_head = head.load()) {
            head.store(old_head->next);
            old_head->~Node(); // 显式调用析构函数
            pool_free(old_head); // 使用内存池释放
        }
    }

    void push(T item) {
        Node* new_node = static_cast<Node*>(pool_malloc(sizeof(Node))); // 使用内存池
        new(new_node) Node(std::move(item)); // placement new
        new_node->next = head.load();

        while (!head.compare_exchange_weak(new_node->next, new_node)) {
            // 重试
        }
    }

    bool pop(T& result) {
        Node* old_head = head.load();

        while (old_head && !head.compare_exchange_weak(old_head, old_head->next)) {
            // 重试
        }

        if (old_head) {
            result = std::move(old_head->data);
            old_head->~Node(); // 显式调用析构函数
            pool_free(old_head); // 使用内存池释放
            return true;
        }

        return false;
    }

    bool empty() const {
        return head.load() == nullptr;
    }
};

// 无锁环形缓冲区 (SPSC Ring Buffer)
template<typename T, size_t Size>
class RingBuffer {
private:
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static constexpr size_t MASK = Size - 1;

    struct alignas(64) Slot {
        std::atomic<T> data{};
        std::atomic<bool> valid{false};
    };

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) Slot slots[Size];

public:
    bool push(T item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & MASK;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // 缓冲区满
        }

        slots[current_tail].data.store(std::move(item), std::memory_order_relaxed);
        slots[current_tail].valid.store(true, std::memory_order_release);
        tail.store(next_tail, std::memory_order_release);

        return true;
    }

    bool pop(T& result) {
        size_t current_head = head.load(std::memory_order_relaxed);

        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // 缓冲区空
        }

        if (!slots[current_head].valid.load(std::memory_order_acquire)) {
            return false;
        }

        result = std::move(slots[current_head].data.load(std::memory_order_relaxed));
        slots[current_head].valid.store(false, std::memory_order_release);
        head.store((current_head + 1) & MASK, std::memory_order_release);

        return true;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    bool full() const {
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t next_tail = (current_tail + 1) & MASK;
        return next_tail == head.load(std::memory_order_acquire);
    }
};

// 高性能原子计数器
class AtomicCounter {
private:
    alignas(64) std::atomic<size_t> count_{0};

public:
    size_t increment() {
        return count_.fetch_add(1, std::memory_order_relaxed);
    }

    size_t decrement() {
        return count_.fetch_sub(1, std::memory_order_relaxed);
    }

    size_t get() const {
        return count_.load(std::memory_order_acquire);
    }

    void set(size_t value) {
        count_.store(value, std::memory_order_release);
    }
};

// 静态成员定义
} // namespace lockfree
