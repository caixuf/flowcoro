#pragma once
#include <memory>
#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace flowcoro {

// 缓存行大小常量
static constexpr size_t CACHE_LINE_SIZE = 64;

// 内存对齐分配器
template<size_t Alignment = CACHE_LINE_SIZE>
class AlignedAllocator {
public:
    static void* allocate(size_t size) {
        void* ptr = nullptr;
        if (posix_memalign(&ptr, Alignment, size) != 0) {
            throw std::bad_alloc();
        }
        return ptr;
    }
    
    static void deallocate(void* ptr) {
        std::free(ptr);
    }
};

// 缓存友好的循环缓冲区
template<typename T, size_t Capacity>
class alignas(CACHE_LINE_SIZE) CacheFriendlyRingBuffer {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;
    
    // 分离读写指针到不同缓存行，避免false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_{0};
    
    // 数据存储区域，对齐到缓存行
    alignas(CACHE_LINE_SIZE) T data_[Capacity];
    
public:
    CacheFriendlyRingBuffer() {
        // 初始化数据
        for (size_t i = 0; i < Capacity; ++i) {
            new (&data_[i]) T();
        }
    }
    
    ~CacheFriendlyRingBuffer() {
        // 清理数据
        for (size_t i = 0; i < Capacity; ++i) {
            data_[i].~T();
        }
    }
    
    // 写入数据（生产者）
    bool push(const T& item) {
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t next_write_pos = (write_pos + 1) & MASK;
        
        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // 缓冲区满
        }
        
        data_[write_pos] = item;
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }
    
    // 移动语义版本
    bool push(T&& item) {
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t next_write_pos = (write_pos + 1) & MASK;
        
        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // 缓冲区满
        }
        
        data_[write_pos] = std::move(item);
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }
    
    // 读取数据（消费者）
    bool pop(T& item) {
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        
        if (read_pos == write_pos_.load(std::memory_order_acquire)) {
            return false; // 缓冲区空
        }
        
        item = std::move(data_[read_pos]);
        read_pos_.store((read_pos + 1) & MASK, std::memory_order_release);
        return true;
    }
    
    // 批量操作 - 更好的缓存局部性
    size_t push_batch(const T* items, size_t count) {
        size_t pushed = 0;
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const size_t read_pos = read_pos_.load(std::memory_order_acquire);
        
        // 计算可用空间
        const size_t available = (read_pos - write_pos - 1) & MASK;
        const size_t to_push = std::min(count, available);
        
        if (to_push == 0) return 0;
        
        // 连续写入，减少缓存miss
        for (size_t i = 0; i < to_push; ++i) {
            data_[(write_pos + i) & MASK] = items[i];
        }
        
        write_pos_.store((write_pos + to_push) & MASK, std::memory_order_release);
        return to_push;
    }
    
    size_t pop_batch(T* items, size_t count) {
        size_t popped = 0;
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        
        // 计算可读数据量
        const size_t available = (write_pos - read_pos) & MASK;
        const size_t to_pop = std::min(count, available);
        
        if (to_pop == 0) return 0;
        
        // 连续读取，减少缓存miss
        for (size_t i = 0; i < to_pop; ++i) {
            items[i] = std::move(data_[(read_pos + i) & MASK]);
        }
        
        read_pos_.store((read_pos + to_pop) & MASK, std::memory_order_release);
        return to_pop;
    }
    
    // 查询状态
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

// 分层缓存友好的内存池
template<typename T>
class CacheFriendlyMemoryPool {
private:
    struct alignas(CACHE_LINE_SIZE) Block {
        T data;
        std::atomic<Block*> next{nullptr};
        
        template<typename... Args>
        Block(Args&&... args) : data(std::forward<Args>(args)...) {}
    };
    
    // 分离到不同缓存行
    alignas(CACHE_LINE_SIZE) std::atomic<Block*> free_head_{nullptr};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> pool_size_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> allocated_count_{0};
    
    // 预分配的内存块
    std::vector<std::unique_ptr<Block[]>> allocated_chunks_;
    std::mutex chunks_mutex_;
    
    static constexpr size_t CHUNK_SIZE = 64; // 每次分配64个对象
    
public:
    CacheFriendlyMemoryPool() {
        expand_pool();
    }
    
    ~CacheFriendlyMemoryPool() = default;
    
    // 获取对象
    template<typename... Args>
    std::unique_ptr<T, std::function<void(T*)>> acquire(Args&&... args) {
        Block* block = free_head_.load(std::memory_order_acquire);
        
        while (block) {
            Block* next = block->next.load(std::memory_order_relaxed);
            if (free_head_.compare_exchange_weak(block, next, std::memory_order_release)) {
                // 成功获取到块
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                
                // 构造对象
                new (&block->data) T(std::forward<Args>(args)...);
                
                // 返回带自定义删除器的unique_ptr
                return std::unique_ptr<T, std::function<void(T*)>>(
                    &block->data,
                    [this, block](T* ptr) {
                        ptr->~T();
                        release(block);
                    }
                );
            }
        }
        
        // 池为空，扩展后重试
        expand_pool();
        return acquire(std::forward<Args>(args)...);
    }
    
    // 获取池统计信息
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
        
        // 分配新的内存块
        auto chunk = std::make_unique<Block[]>(CHUNK_SIZE);
        
        // 链接所有块
        for (size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
            chunk[i].next.store(&chunk[i + 1], std::memory_order_relaxed);
        }
        chunk[CHUNK_SIZE - 1].next.store(nullptr, std::memory_order_relaxed);
        
        // 添加到空闲链表
        Block* old_head = free_head_.load(std::memory_order_relaxed);
        do {
            chunk[CHUNK_SIZE - 1].next.store(old_head, std::memory_order_relaxed);
        } while (!free_head_.compare_exchange_weak(old_head, &chunk[0], std::memory_order_release));
        
        pool_size_.fetch_add(CHUNK_SIZE, std::memory_order_relaxed);
        allocated_chunks_.push_back(std::move(chunk));
    }
};

// 缓存友好的字符串缓冲区
class StringBuffer {
private:
    static constexpr size_t DEFAULT_CAPACITY = 4096;
    static constexpr size_t MAX_SMALL_STRING = 23; // SSO阈值
    
    alignas(CACHE_LINE_SIZE) char* data_;
    alignas(CACHE_LINE_SIZE) size_t capacity_;
    alignas(CACHE_LINE_SIZE) size_t size_;
    
    // 小字符串优化
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
    
    // 禁止拷贝，允许移动
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
    
    // 追加字符串
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
    
    // 格式化追加
    template<typename... Args>
    void append_format(const char* format, Args&&... args) {
        char temp_buffer[1024];
        int len = std::snprintf(temp_buffer, sizeof(temp_buffer), format, std::forward<Args>(args)...);
        if (len > 0) {
            append(temp_buffer, static_cast<size_t>(len));
        }
    }
    
    // 清空缓冲区
    void clear() {
        size_ = 0;
        data_[0] = '\0';
    }
    
    // 重置大小
    void resize(size_t new_size) {
        ensure_capacity(new_size + 1);
        size_ = new_size;
        data_[size_] = '\0';
    }
    
    // 访问器
    const char* c_str() const { return data_; }
    const char* data() const { return data_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    
    // 转换为std::string
    std::string to_string() const {
        return std::string(data_, size_);
    }
    
private:
    void ensure_capacity(size_t required) {
        if (required <= capacity_) return;
        
        // 计算新容量（2的倍数增长）
        size_t new_capacity = capacity_;
        while (new_capacity < required) {
            new_capacity *= 2;
        }
        
        if (use_small_buffer_ && new_capacity > MAX_SMALL_STRING) {
            // 从小缓冲区迁移到大缓冲区
            char* new_data = static_cast<char*>(AlignedAllocator<>::allocate(new_capacity));
            std::memcpy(new_data, small_buffer_, size_ + 1);
            data_ = new_data;
            use_small_buffer_ = false;
            capacity_ = new_capacity;
        } else if (!use_small_buffer_) {
            // 扩展大缓冲区
            char* new_data = static_cast<char*>(AlignedAllocator<>::allocate(new_capacity));
            std::memcpy(new_data, data_, size_ + 1);
            AlignedAllocator<>::deallocate(data_);
            data_ = new_data;
            capacity_ = new_capacity;
        }
    }
};

} // namespace flowcoro
