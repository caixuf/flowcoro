#pragma once
#include <atomic>
#include <memory>
#include <type_traits>
#include "memory_pool.h"

namespace lockfree {

// flowcoro
using flowcoro::pool_malloc;
using flowcoro::pool_free;

// 
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
                data_ptr->~T(); // 
                pool_free(data_ptr); // 
            }
        }
    };

    alignas(64) std::atomic<Node*> head;
    alignas(64) std::atomic<Node*> tail;
    alignas(64) std::atomic<bool> destroyed{false}; // 

public:
    Queue() {
        Node* dummy = static_cast<Node*>(pool_malloc(sizeof(Node))); // 
        new(dummy) Node(); // placement new
        head.store(dummy);
        tail.store(dummy);
        destroyed.store(false);
    }

    ~Queue() {
        // 
        destroyed.store(true, std::memory_order_release);

        // 
        T unused_item;
        while (dequeue_unsafe(unused_item)) {
            // 
        }

        // 
        Node* current = head.load();
        while (current) {
            Node* next = current->next.load();
            current->~Node(); // 
            pool_free(current); // 
            current = next;
        }
    }

    void enqueue(T item) {
        if (destroyed.load(std::memory_order_acquire)) {
            return; // 
        }

        // Node
        struct NodeWithData {
            Node node;
            alignas(T) char data_storage[sizeof(T)];
        };
        
        NodeWithData* block = static_cast<NodeWithData*>(pool_malloc(sizeof(NodeWithData)));
        new(&block->node) Node(); // placement new for Node
        
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
            return false; // 
        }

        return dequeue_unsafe(result);
    }

private:
    bool dequeue_unsafe(T& result) {
        Node* head_node = head.load();
        if (!head_node) {
            return false; // 
        }

        Node* next = head_node->next.load();
        if (next == nullptr) {
            return false; // 
        }

        T* data_ptr = next->data.exchange(nullptr);
        if (data_ptr == nullptr) {
            return false; // 
        }

        result = *data_ptr;
        data_ptr->~T(); // 

        // head
        Node* expected = head_node;
        if (head.compare_exchange_weak(expected, next)) {
            head_node->~Node(); // 
            
            // Node
            struct NodeWithData {
                Node node;
                alignas(T) char data_storage[sizeof(T)];
            };
            NodeWithData* block = reinterpret_cast<NodeWithData*>(
                reinterpret_cast<char*>(head_node) - offsetof(NodeWithData, node));
            pool_free(block); // 
        }

        return true;
    }

public:

    bool empty() const {
        Node* head_node = head.load();
        return head_node->next.load() == nullptr;
    }

    // 
    size_t size_estimate() const {
        if (destroyed.load(std::memory_order_acquire)) {
            return 0;
        }
        
        size_t count = 0;
        Node* current = head.load();
        if (current) {
            current = current->next.load(); // 
            while (current && count < 10000) { // 
                current = current->next.load();
                count++;
            }
        }
        return count;
    }
};

//  (Treiber Stack) - 
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
            old_head->~Node(); // 
            pool_free(old_head); // 
        }
    }

    void push(T item) {
        Node* new_node = static_cast<Node*>(pool_malloc(sizeof(Node))); // 
        new(new_node) Node(std::move(item)); // placement new
        new_node->next = head.load();

        while (!head.compare_exchange_weak(new_node->next, new_node)) {
            // 
        }
    }

    bool pop(T& result) {
        Node* old_head = head.load();

        while (old_head && !head.compare_exchange_weak(old_head, old_head->next)) {
            // 
        }

        if (old_head) {
            result = std::move(old_head->data);
            old_head->~Node(); // 
            pool_free(old_head); // 
            return true;
        }

        return false;
    }

    bool empty() const {
        return head.load() == nullptr;
    }
};

//  (SPSC Ring Buffer)
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
            return false; // 
        }

        slots[current_tail].data.store(std::move(item), std::memory_order_relaxed);
        slots[current_tail].valid.store(true, std::memory_order_release);
        tail.store(next_tail, std::memory_order_release);

        return true;
    }

    bool pop(T& result) {
        size_t current_head = head.load(std::memory_order_relaxed);

        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // 
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

// 
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

// 
} // namespace lockfree
