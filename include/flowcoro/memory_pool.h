#pragma once
#include <cstddef>
#include <vector>
#include <mutex>
#include <memory>
#include <list>

namespace flowcoro {

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size = 4096, size_t initial_block_count = 128)
        : block_size_(block_size), 
          initial_block_count_(initial_block_count),
          expansion_factor_(2.0),  // 每次扩展为当前大小的2倍
          max_total_blocks_(initial_block_count * 32) {  // 最大扩展限制
        
        expand_pool(initial_block_count);
    }
    
    ~MemoryPool() {
        // unique_ptr会自动清理所有内存块
    }
    
    void* allocate() {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (free_blocks_.empty()) {
            // 动态扩展池
            size_t current_total = total_allocated_blocks_;
            size_t expand_size = std::max(
                static_cast<size_t>(current_total * (expansion_factor_ - 1.0)),
                initial_block_count_ / 4  // 最小扩展数量
            );
            
            // 检查是否超过最大限制
            if (current_total + expand_size > max_total_blocks_) {
                expand_size = max_total_blocks_ - current_total;
            }
            
            if (expand_size > 0) {
                expand_pool(expand_size);
            }
        }
        
        if (free_blocks_.empty()) {
            // 如果还是没有可用块，说明已经达到最大限制
            // 这时我们可以选择降低扩展因子继续尝试，或者抛出异常
            // 这里我们尝试小幅扩展
            if (total_allocated_blocks_ < max_total_blocks_) {
                expand_pool(1);  // 至少分配一个块
            }
        }
        
        if (free_blocks_.empty()) {
            throw std::bad_alloc();  // 真的无法分配时抛出异常
        }
        
        void* ptr = free_blocks_.back();
        free_blocks_.pop_back();
        allocated_count_++;
        return ptr;
    }
    
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mtx_);
        
        // 检查指针是否来自我们的池
        if (!is_from_pool(ptr)) {
            // 不是来自池的内存，抛出异常而不是静默忽略
            throw std::invalid_argument("Attempting to deallocate pointer not from this pool");
        }
        
        free_blocks_.push_back(ptr);
        allocated_count_--;
    }
    
    // 配置接口
    void set_expansion_factor(double factor) {
        std::lock_guard<std::mutex> lock(mtx_);
        expansion_factor_ = std::max(1.1, std::min(factor, 5.0));  // 限制在合理范围
    }
    
    void set_max_total_blocks(size_t max_blocks) {
        std::lock_guard<std::mutex> lock(mtx_);
        max_total_blocks_ = std::max(max_blocks, initial_block_count_);
    }
    
    // 统计信息
    struct PoolStats {
        size_t block_size;
        size_t total_blocks;
        size_t free_blocks;
        size_t allocated_blocks;
        size_t memory_chunks;
        size_t total_memory_bytes;
    };
    
    PoolStats get_stats() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return {
            block_size_,
            total_allocated_blocks_,
            free_blocks_.size(),
            allocated_count_,
            memory_chunks_.size(),
            total_allocated_blocks_ * block_size_
        };
    }
    
    size_t block_size() const { return block_size_; }
    size_t available_blocks() const { 
        std::lock_guard<std::mutex> lock(mtx_);
        return free_blocks_.size(); 
    }
    
private:
    struct MemoryChunk {
        std::unique_ptr<char[]> memory;
        char* start;
        char* end;
        size_t block_count;
        
        MemoryChunk(size_t block_size, size_t count) 
            : memory(std::make_unique<char[]>(block_size * count)),
              start(memory.get()),
              end(start + (block_size * count)),
              block_count(count) {}
    };
    
    bool is_from_pool(void* ptr) const {
        char* char_ptr = static_cast<char*>(ptr);
        for (const auto& chunk : memory_chunks_) {
            if (char_ptr >= chunk->start && char_ptr < chunk->end) {
                return true;
            }
        }
        return false;
    }
    
    void expand_pool(size_t additional_blocks) {
        if (additional_blocks == 0) return;
        
        // 创建新的内存块
        auto chunk = std::make_unique<MemoryChunk>(block_size_, additional_blocks);
        char* memory_start = chunk->start;
        
        // 将新内存切分成小块加入空闲列表
        for (size_t i = 0; i < additional_blocks; ++i) {
            void* block = memory_start + (i * block_size_);
            free_blocks_.push_back(block);
        }
        
        total_allocated_blocks_ += additional_blocks;
        memory_chunks_.push_back(std::move(chunk));
    }
    
    size_t block_size_;
    size_t initial_block_count_;
    double expansion_factor_;
    size_t max_total_blocks_;
    size_t total_allocated_blocks_ = 0;
    size_t allocated_count_ = 0;
    
    std::list<std::unique_ptr<MemoryChunk>> memory_chunks_;  // 所有内存块
    std::vector<void*> free_blocks_;                         // 空闲块列表
    mutable std::mutex mtx_;                                 // 线程安全
};

} // namespace flowcoro
