#pragma once
#include <memory>
#include <new>
#include <cstddef>

namespace flowcoro {

// 缓存对齐的协程promise分配器
// 提高缓存局部性，减少false sharing
template<typename T>
class CacheAlignedAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    // C++20 alignas支持
    static constexpr size_t alignment = std::hardware_destructive_interference_size;
    
    CacheAlignedAllocator() noexcept = default;
    
    template<typename U>
    CacheAlignedAllocator(const CacheAlignedAllocator<U>&) noexcept {}
    
    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        
        size_t size = n * sizeof(T);
        // 对齐到缓存行
        void* ptr = ::operator new(size, std::align_val_t{alignment});
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<T*>(ptr);
    }
    
    void deallocate(T* ptr, size_t) noexcept {
        ::operator delete(ptr, std::align_val_t{alignment});
    }
    
    template<typename U>
    bool operator==(const CacheAlignedAllocator<U>&) const noexcept {
        return true;
    }
    
    template<typename U>
    bool operator!=(const CacheAlignedAllocator<U>&) const noexcept {
        return false;
    }
};

// 为promise_type提供自定义分配器支持
// 可以在promise_type中添加以下方法来使用：
// void* operator new(std::size_t size) {
//     CacheAlignedAllocator<promise_type> alloc;
//     return alloc.allocate(1);
// }
// 
// void operator delete(void* ptr) noexcept {
//     CacheAlignedAllocator<promise_type> alloc;
//     alloc.deallocate(static_cast<promise_type*>(ptr), 1);
// }

} // namespace flowcoro
