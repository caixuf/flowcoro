#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <new>

namespace flowcoro {

// Redis zmallocNginx
// lockfreeNode
class SimpleMemoryPool {
private:
    // Redis
    struct MemoryBlock {
        MemoryBlock* next;
        size_t size;
        alignas(std::max_align_t) char data[];
    };

    // Nginx
    struct FreeList {
        std::atomic<MemoryBlock*> head{nullptr};
        std::atomic<size_t> count{0};
        static constexpr size_t MAX_CACHED = 64; // 
    };

    // Node
    static constexpr size_t SIZE_CLASSES[] = {32, 64, 128, 256, 512, 1024};
    static constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);
    
    std::array<FreeList, NUM_SIZE_CLASSES> free_lists;
    
    // Redismemory tracking
    std::atomic<size_t> total_allocated{0};
    std::atomic<size_t> total_freed{0};

    // 
    size_t get_size_class_index(size_t size) const noexcept {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            if (size <= SIZE_CLASSES[i]) {
                return i;
            }
        }
        return NUM_SIZE_CLASSES; // 
    }

    // lockfree
    MemoryBlock* pop_from_freelist(FreeList& list) noexcept {
        MemoryBlock* head = list.head.load(std::memory_order_acquire);
        while (head != nullptr) {
            if (list.head.compare_exchange_weak(head, head->next, 
                                              std::memory_order_release, 
                                              std::memory_order_acquire)) {
                list.count.fetch_sub(1, std::memory_order_relaxed);
                return head;
            }
        }
        return nullptr;
    }

    void push_to_freelist(FreeList& list, MemoryBlock* block) noexcept {
        // 
        if (list.count.load(std::memory_order_relaxed) >= FreeList::MAX_CACHED) {
            // 
            std::free(block);
            total_freed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        MemoryBlock* head = list.head.load(std::memory_order_acquire);
        do {
            block->next = head;
        } while (!list.head.compare_exchange_weak(head, block,
                                                std::memory_order_release,
                                                std::memory_order_acquire));
        list.count.fetch_add(1, std::memory_order_relaxed);
    }

public:
    SimpleMemoryPool() = default;
    
    // 
    SimpleMemoryPool(size_t block_size, size_t initial_count) : SimpleMemoryPool() {
        // 
        (void)block_size;
        (void)initial_count;
    }
    
    ~SimpleMemoryPool() {
        // 
        for (auto& list : free_lists) {
            MemoryBlock* block = list.head.load();
            while (block) {
                MemoryBlock* next = block->next;
                std::free(block);
                block = next;
            }
        }
    }

    // 
    SimpleMemoryPool(const SimpleMemoryPool&) = delete;
    SimpleMemoryPool& operator=(const SimpleMemoryPool&) = delete;

    // allocate
    void* allocate() noexcept {
        return allocate(64); // 64Node
    }

    void* allocate(size_t size) noexcept {
        size_t index = get_size_class_index(size);
        
        if (index >= NUM_SIZE_CLASSES) {
            // 
            MemoryBlock* block = static_cast<MemoryBlock*>(
                std::malloc(sizeof(MemoryBlock) + size));
            if (!block) return nullptr;
            
            block->size = size;
            total_allocated.fetch_add(1, std::memory_order_relaxed);
            return block->data;
        }

        // 
        FreeList& list = free_lists[index];
        MemoryBlock* block = pop_from_freelist(list);
        
        if (!block) {
            // 
            size_t actual_size = SIZE_CLASSES[index];
            block = static_cast<MemoryBlock*>(
                std::malloc(sizeof(MemoryBlock) + actual_size));
            if (!block) return nullptr;
            
            block->size = actual_size;
            total_allocated.fetch_add(1, std::memory_order_relaxed);
        }

        return block->data;
    }

    void deallocate(void* ptr) noexcept {
        if (!ptr) return;

        // MemoryBlock
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
            static_cast<char*>(ptr) - offsetof(MemoryBlock, data));

        size_t index = get_size_class_index(block->size);
        
        if (index >= NUM_SIZE_CLASSES) {
            // 
            std::free(block);
            total_freed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // 
        FreeList& list = free_lists[index];
        push_to_freelist(list, block);
    }

    // 
    size_t get_allocated_count() const noexcept {
        return total_allocated.load(std::memory_order_relaxed);
    }

    size_t get_freed_count() const noexcept {
        return total_freed.load(std::memory_order_relaxed);
    }

    // Redis
    static SimpleMemoryPool& instance() noexcept {
        static SimpleMemoryPool pool;
        return pool;
    }
};

// Rediszmalloc/zfree
inline void* pool_malloc(size_t size) noexcept {
    return SimpleMemoryPool::instance().allocate(size);
}

inline void pool_free(void* ptr) noexcept {
    SimpleMemoryPool::instance().deallocate(ptr);
}

// RAII
template<typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() = default;
    template<typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        void* ptr = pool_malloc(n * sizeof(T));
        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, size_t) noexcept {
        pool_free(ptr);
    }

    template<typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept { return true; }
    
    template<typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }
};

// MemoryPool
using MemoryPool = SimpleMemoryPool;

} // namespace flowcoro
