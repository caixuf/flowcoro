#pragma once
#include <memory>
#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include "memory_pool.h"

namespace flowcoro {

// 
static constexpr size_t CACHE_LINE_SIZE = 64;

//  - 
template<size_t Alignment = CACHE_LINE_SIZE>
class AlignedAllocator {
public:
    static void* allocate(size_t size) {
        // <= 1024
        if (size <= 1024) {
            void* ptr = pool_malloc(size);
            if (!ptr) throw std::bad_alloc();
            return ptr;
        }
        
        // 
        void* ptr = nullptr;
        if (posix_memalign(&ptr, Alignment, size) != 0) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    static void deallocate(void* ptr) {
        // fallback
        pool_free(ptr);
    }
};

// 
template<typename T, size_t Capacity>
class alignas(CACHE_LINE_SIZE) CacheFriendlyRingBuffer {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    // false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_{0};

    // 
    alignas(CACHE_LINE_SIZE) T data_[Capacity];

public:
    CacheFriendlyRingBuffer() {
        // 
        for (size_t i = 0; i < Capacity; ++i) {
            new (&data_[i]) T();
        }
    }

    ~CacheFriendlyRingBuffer() {
        // 
        for (size_t i = 0; i < Capacity; ++i) {
            data_[i].~T();
        }
    }

    // 
    bool push(const T& item) {
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t next_write_pos = (write_pos + 1) & MASK;

        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // 
        }

        data_[write_pos] = item;
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }

    // 
    bool push(T&& item) {
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t next_write_pos = (write_pos + 1) & MASK;

        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // 
        }

        data_[write_pos] = std::move(item);
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }

    // 
    bool pop(T& item) {
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);

        if (read_pos == write_pos_.load(std::memory_order_acquire)) {
            return false; // 
        }

        item = std::move(data_[read_pos]);
        read_pos_.store((read_pos + 1) & MASK, std::memory_order_release);
        return true;
    }

    //  - 
    size_t push_batch(const T* items, size_t count) {
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t read_pos = read_pos_.load(std::memory_order_acquire);

        // 
        const size_t available = (read_pos - write_pos - 1) & MASK;
        const size_t to_push = std::min(count, available);

        if (to_push == 0) return 0;

        // miss
        for (size_t i = 0; i < to_push; ++i) {
            data_[(write_pos + i) & MASK] = items[i];
        }

        write_pos_.store((write_pos + to_push) & MASK, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* items, size_t count) {
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);

        // 
        const size_t available = (write_pos - read_pos) & MASK;
        const size_t to_pop = std::min(count, available);

        if (to_pop == 0) return 0;

        // miss
        for (size_t i = 0; i < to_pop; ++i) {
            items[i] = std::move(data_[(read_pos + i) & MASK]);
        }

        read_pos_.store((read_pos + to_pop) & MASK, std::memory_order_release);
        return to_pop;
    }

    // 
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) ==
               write_pos_.load(std::memory_order_acquire);
    }

    bool full() const {
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        const size_t read_pos = read_pos_.load(std::memory_order_acquire);
        return ((write_pos + 1) & MASK) == read_pos;
    }

    size_t size() const {
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        const size_t read_pos = read_pos_.load(std::memory_order_acquire);
        return (write_pos - read_pos) & MASK;
    }

    static constexpr size_t capacity() { return Capacity; }
};

// 
template<typename T>
class CacheFriendlyMemoryPool {
private:
    struct alignas(CACHE_LINE_SIZE) Block {
        T data;
        std::atomic<Block*> next{nullptr};

        template<typename... Args>
        Block(Args&&... args) : data(std::forward<Args>(args)...) {}
    };

    // 
    alignas(CACHE_LINE_SIZE) std::atomic<Block*> free_head_{nullptr};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> pool_size_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> allocated_count_{0};

    // 
    std::vector<std::unique_ptr<Block[]>> allocated_chunks_;
    std::mutex chunks_mutex_;

    static constexpr size_t CHUNK_SIZE = 64; // 64

public:
    CacheFriendlyMemoryPool() {
        expand_pool();
    }

    ~CacheFriendlyMemoryPool() = default;

    // 
    template<typename... Args>
    std::unique_ptr<T, std::function<void(T*)>> acquire(Args&&... args) {
        Block* block = free_head_.load(std::memory_order_acquire);

        while (block) {
            Block* next = block->next.load(std::memory_order_relaxed);
            if (free_head_.compare_exchange_weak(block, next, std::memory_order_release)) {
                // 
                allocated_count_.fetch_add(1, std::memory_order_relaxed);

                // 
                new (&block->data) T(std::forward<Args>(args)...);

                // unique_ptr
                return std::unique_ptr<T, std::function<void(T*)>>(
                    &block->data,
                    [this, block](T* ptr) {
                        ptr->~T();
                        release(block);
                    }
                );
            }
        }

        // 
        expand_pool();
        return acquire(std::forward<Args>(args)...);
    }

    // 
    struct PoolStats {
        size_t pool_size;
        size_t allocated_count;
        size_t free_count;
        double utilization;
    };

    PoolStats get_stats() const {
        size_t pool = pool_size_.load(std::memory_order_acquire);
        size_t allocated = allocated_count_.load(std::memory_order_acquire);
        size_t free = pool - allocated;
        double util = pool > 0 ? (double)allocated / pool * 100.0 : 0.0;

        return {pool, allocated, free, util};
    }

private:
    void release(Block* block) {
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);

        Block* old_head = free_head_.load(std::memory_order_relaxed);
        do {
            block->next.store(old_head, std::memory_order_relaxed);
        } while (!free_head_.compare_exchange_weak(old_head, block, std::memory_order_release));
    }

    void expand_pool() {
        std::lock_guard<std::mutex> lock(chunks_mutex_);

        // 
        auto chunk = std::make_unique<Block[]>(CHUNK_SIZE);

        // 
        for (size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
            chunk[i].next.store(&chunk[i + 1], std::memory_order_relaxed);
        }
        chunk[CHUNK_SIZE - 1].next.store(nullptr, std::memory_order_relaxed);

        // 
        Block* old_head = free_head_.load(std::memory_order_relaxed);
        do {
            chunk[CHUNK_SIZE - 1].next.store(old_head, std::memory_order_relaxed);
        } while (!free_head_.compare_exchange_weak(old_head, &chunk[0], std::memory_order_release));

        pool_size_.fetch_add(CHUNK_SIZE, std::memory_order_relaxed);
        allocated_chunks_.push_back(std::move(chunk));
    }
};

// 
class StringBuffer {
private:
    static constexpr size_t DEFAULT_CAPACITY = 4096;
    static constexpr size_t MAX_SMALL_STRING = 23; // SSO

    alignas(CACHE_LINE_SIZE) char* data_;
    alignas(CACHE_LINE_SIZE) size_t capacity_;
    alignas(CACHE_LINE_SIZE) size_t size_;

    // 
    alignas(CACHE_LINE_SIZE) char small_buffer_[MAX_SMALL_STRING + 1];
    bool use_small_buffer_;

public:
    StringBuffer(size_t initial_capacity = DEFAULT_CAPACITY)
        : capacity_(initial_capacity), size_(0), use_small_buffer_(true) {

        if (initial_capacity > MAX_SMALL_STRING) {
            data_ = static_cast<char*>(AlignedAllocator<>::allocate(capacity_));
            use_small_buffer_ = false;
        } else {
            data_ = small_buffer_;
        }

        data_[0] = '\0';
    }

    ~StringBuffer() {
        if (!use_small_buffer_) {
            AlignedAllocator<>::deallocate(data_);
        }
    }

    // 
    StringBuffer(const StringBuffer&) = delete;
    StringBuffer& operator=(const StringBuffer&) = delete;

    StringBuffer(StringBuffer&& other) noexcept
        : capacity_(other.capacity_), size_(other.size_), use_small_buffer_(other.use_small_buffer_) {

        if (use_small_buffer_) {
            std::memcpy(small_buffer_, other.small_buffer_, size_ + 1);
            data_ = small_buffer_;
        } else {
            data_ = other.data_;
            other.data_ = nullptr;
        }

        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 
    void append(const char* str, size_t len) {
        ensure_capacity(size_ + len + 1);
        std::memcpy(data_ + size_, str, len);
        size_ += len;
        data_[size_] = '\0';
    }

    void append(const std::string& str) {
        append(str.c_str(), str.length());
    }

    void append(char c) {
        ensure_capacity(size_ + 2);
        data_[size_++] = c;
        data_[size_] = '\0';
    }

    // 
    template<typename... Args>
    void append_format(const char* format, Args&&... args) {
        char temp_buffer[1024];
        int len = std::snprintf(temp_buffer, sizeof(temp_buffer), format, std::forward<Args>(args)...);
        if (len > 0) {
            append(temp_buffer, static_cast<size_t>(len));
        }
    }

    // 
    void clear() {
        size_ = 0;
        data_[0] = '\0';
    }

    // 
    void resize(size_t new_size) {
        ensure_capacity(new_size + 1);
        size_ = new_size;
        data_[size_] = '\0';
    }

    // 
    const char* c_str() const { return data_; }
    const char* data() const { return data_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    // std::string
    std::string to_string() const {
        return std::string(data_, size_);
    }

private:
    void ensure_capacity(size_t required) {
        if (required <= capacity_) return;

        // 2
        size_t new_capacity = capacity_;
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        if (use_small_buffer_ && new_capacity > MAX_SMALL_STRING) {
            // 
            char* new_data = static_cast<char*>(AlignedAllocator<>::allocate(new_capacity));
            std::memcpy(new_data, small_buffer_, size_ + 1);
            data_ = new_data;
            use_small_buffer_ = false;
            capacity_ = new_capacity;
        } else if (!use_small_buffer_) {
            // 
            char* new_data = static_cast<char*>(AlignedAllocator<>::allocate(new_capacity));
            std::memcpy(new_data, data_, size_ + 1);
            AlignedAllocator<>::deallocate(data_);
            data_ = new_data;
            capacity_ = new_capacity;
        }
    }
};

} // namespace flowcoro
