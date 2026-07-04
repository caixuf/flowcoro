#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <new>

/*
 * TSAN annotations for lockfree memory pool:
 * When a block is returned to the freelist, we __tsan_release it so TSAN knows
 * all writes to that block are complete.  When the same block is handed out
 * again, we __tsan_acquire it so TSAN links the new access to the prior release.
 * Without these hints, TSAN cannot see the happens-before through CAS-based
 * freelist operations and reports false-positive data races on recycled nodes.
 */
#if defined(__SANITIZE_THREAD__) || defined(__has_feature)
#  if defined(__has_feature)
#    if __has_feature(thread_sanitizer)
#      define FLOWCORO_TSAN_ENABLED 1
#    endif
#  else
#    define FLOWCORO_TSAN_ENABLED 1
#  endif
#endif

#ifdef FLOWCORO_TSAN_ENABLED
extern "C" {
void __tsan_release(void*);
void __tsan_acquire(void*);
}
#  define POOL_TSAN_RELEASE(ptr) __tsan_release((void*)(ptr))
#  define POOL_TSAN_ACQUIRE(ptr) __tsan_acquire((void*)(ptr))
#else
#  define POOL_TSAN_RELEASE(ptr) (void)(ptr)
#  define POOL_TSAN_ACQUIRE(ptr) (void)(ptr)
#endif

namespace flowcoro {

// 基于Redis zmalloc和Nginx内存池设计的轻量级内存池
// 专门为lockfree队列和栈的Node分配优化
class SimpleMemoryPool {
private:
    // 内存块结构，类似Redis的方式
    struct MemoryBlock {
        MemoryBlock* next;
        size_t size;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        alignas(std::max_align_t) char data[];
#pragma GCC diagnostic pop
    };

    // 线程本地自由列表，借鉴Nginx的设计思路
    struct FreeList {
        std::atomic<MemoryBlock*> head{nullptr};
        std::atomic<size_t> count{0};
        static constexpr size_t MAX_CACHED = 64; // 限制缓存数量避免内存泄漏
    };

    // 支持多种常见大小，覆盖Node等小对象
    static constexpr size_t SIZE_CLASSES[] = {32, 64, 128, 256, 512, 1024};
    static constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

    std::array<FreeList, NUM_SIZE_CLASSES> free_lists;

    // 统计信息，类似Redis的memory tracking
    std::atomic<size_t> total_allocated{0};
    std::atomic<size_t> total_freed{0};

    // 获取大小对应的索引
    size_t get_size_class_index(size_t size) const noexcept {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            if (size <= SIZE_CLASSES[i]) {
                return i;
            }
        }
        return NUM_SIZE_CLASSES; // 超出范围，使用系统分配
    }

    // 原子操作的栈操作，借鉴lockfree设计
    MemoryBlock* pop_from_freelist(FreeList& list) noexcept {
        MemoryBlock* head = list.head.load(std::memory_order_acquire);
        while (head != nullptr) {
            if (list.head.compare_exchange_weak(head, head->next,
                                              std::memory_order_release,
                                              std::memory_order_acquire)) {
                list.count.fetch_sub(1, std::memory_order_relaxed);
                POOL_TSAN_ACQUIRE(head->data);
                return head;
            }
        }
        return nullptr;
    }

    void push_to_freelist(FreeList& list, MemoryBlock* block) noexcept {
        POOL_TSAN_RELEASE(block->data);

        // 限制缓存大小，避免内存无限增长
        if (list.count.load(std::memory_order_relaxed) >= FreeList::MAX_CACHED) {
            // 直接释放到系统
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

    // 向后兼容的构造函数
    SimpleMemoryPool(size_t block_size, size_t initial_count) : SimpleMemoryPool() {
        // 新设计中这些参数不再需要，但为了兼容性保留接口
        (void)block_size;
        (void)initial_count;
    }

    ~SimpleMemoryPool() {
        // 清理所有缓存的内存块
        for (auto& list : free_lists) {
            MemoryBlock* block = list.head.load();
            while (block) {
                MemoryBlock* next = block->next;
                std::free(block);
                block = next;
            }
        }
    }

    // 禁止拷贝和移动
    SimpleMemoryPool(const SimpleMemoryPool&) = delete;
    SimpleMemoryPool& operator=(const SimpleMemoryPool&) = delete;

    // 向后兼容的无参数allocate方法
    void* allocate() noexcept {
        return allocate(64); // 默认64字节，对应大多数Node大小
    }

    void* allocate(size_t size) noexcept {
        size_t index = get_size_class_index(size);

        if (index >= NUM_SIZE_CLASSES) {
            // 大对象直接使用系统分配
            MemoryBlock* block = static_cast<MemoryBlock*>(
                std::malloc(sizeof(MemoryBlock) + size));
            if (!block) return nullptr;

            block->size = size;
            total_allocated.fetch_add(1, std::memory_order_relaxed);
            return block->data;
        }

        // 尝试从自由列表获取
        FreeList& list = free_lists[index];
        MemoryBlock* block = pop_from_freelist(list);

        if (!block) {
            // 分配新块
            size_t actual_size = SIZE_CLASSES[index];
            block = static_cast<MemoryBlock*>(
                std::malloc(sizeof(MemoryBlock) + actual_size));
            if (!block) return nullptr;

            block->size = actual_size;
            total_allocated.fetch_add(1, std::memory_order_relaxed);
            // Fresh memory from malloc — no acquire needed
        }

        return block->data;
    }

    void deallocate(void* ptr) noexcept {
        if (!ptr) return;

        // 通过指针算术获取MemoryBlock
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
            static_cast<char*>(ptr) - offsetof(MemoryBlock, data));

        size_t index = get_size_class_index(block->size);

        if (index >= NUM_SIZE_CLASSES) {
            // 大对象直接释放
            std::free(block);
            total_freed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // 放回自由列表
        FreeList& list = free_lists[index];
        push_to_freelist(list, block);
    }

    // 获取统计信息
    size_t get_allocated_count() const noexcept {
        return total_allocated.load(std::memory_order_relaxed);
    }

    size_t get_freed_count() const noexcept {
        return total_freed.load(std::memory_order_relaxed);
    }

    // 全局单例，类似Redis的全局内存管理
    static SimpleMemoryPool& instance() noexcept {
        static SimpleMemoryPool pool;
        return pool;
    }
};

// 便利函数，类似Redis的zmalloc/zfree
inline void* pool_malloc(size_t size) noexcept {
    return SimpleMemoryPool::instance().allocate(size);
}

inline void pool_free(void* ptr) noexcept {
    SimpleMemoryPool::instance().deallocate(ptr);
}

// RAII包装器，用于自动内存管理
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

// 向后兼容的MemoryPool类型别名
using MemoryPool = SimpleMemoryPool;

} // namespace flowcoro
