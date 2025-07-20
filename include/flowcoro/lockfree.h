#pragma once
#include <atomic>
#include <memory>
#include <type_traits>

namespace lockfree {

// 无锁队列实现 (简化版本，更安全)
template<typename T>
class Queue {
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
        
        Node() = default;
        ~Node() {
            T* data_ptr = data.load();
            if (data_ptr) {
                delete data_ptr;
            }
        }
    };
    
    alignas(64) std::atomic<Node*> head;
    alignas(64) std::atomic<Node*> tail;
    alignas(64) std::atomic<bool> destroyed{false}; // 添加析构标志
    
public:
    Queue() {
        Node* dummy = new Node;
        head.store(dummy);
        tail.store(dummy);
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
        
        // 然后清理节点链表
        Node* current = head.load();
        while (current) {
            Node* next = current->next.load();
            delete current;
            current = next;
        }
    }
    
    void enqueue(T item) {
        if (destroyed.load(std::memory_order_acquire)) {
            return; // 队列已析构，丢弃任务
        }
        
        Node* new_node = new Node;
        T* data_ptr = new T(std::move(item));
        new_node->data.store(data_ptr);
        
        Node* prev_tail = tail.exchange(new_node);
        if (prev_tail) {
            prev_tail->next.store(new_node);
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
        delete data_ptr;
        
        // 尝试更新head，如果失败也不要紧，下次调用会重试
        Node* expected = head_node;
        if (head.compare_exchange_weak(expected, next)) {
            delete head_node;
        }
        
        return true;
    }
    
public:
    
    bool empty() const {
        Node* head_node = head.load();
        return head_node->next.load() == nullptr;
    }
};

// 无锁栈实现 (Treiber Stack)
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
            delete old_head;
        }
    }
    
    void push(T item) {
        Node* new_node = new Node(std::move(item));
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
            delete old_head;
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

} // namespace lockfree
