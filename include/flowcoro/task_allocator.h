#pragma once
#include <memory>
#include <new>
#include <cstddef>

namespace flowcoro {

// promise
// false sharing
template<typename T>
class CacheAlignedAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    // C++20 alignas
    static constexpr size_t alignment = std::hardware_destructive_interference_size;
    
    CacheAlignedAllocator() noexcept = default;
    
    template<typename U>
    CacheAlignedAllocator(const CacheAlignedAllocator<U>&) noexcept {}
    
    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        
        size_t size = n * sizeof(T);
        // 
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

// promise_type
// promise_type
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
