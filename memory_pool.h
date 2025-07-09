#pragma once
#include <cstddef>
#include <vector>
#include <mutex>

namespace flowcoro {

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size = 4096, size_t block_count = 128)
        : block_size_(block_size), block_count_(block_count) {
        for (size_t i = 0; i < block_count_; ++i) {
            free_blocks_.push_back(::operator new(block_size_));
        }
    }
    ~MemoryPool() {
        for (void* ptr : free_blocks_) {
            ::operator delete(ptr);
        }
    }
    void* allocate() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (free_blocks_.empty()) {
            return ::operator new(block_size_);
        }
        void* ptr = free_blocks_.back();
        free_blocks_.pop_back();
        return ptr;
    }
    void deallocate(void* ptr) {
        std::lock_guard<std::mutex> lock(mtx_);
        free_blocks_.push_back(ptr);
    }
    size_t block_size() const { return block_size_; }
private:
    size_t block_size_;
    size_t block_count_;
    std::vector<void*> free_blocks_;
    std::mutex mtx_;
};

} // namespace flowcoro
