/**
 * @file memory.h
 * @brief FlowCoro内存管理组件
 */

#pragma once
#include "memory_pool.h"
#include "object_pool.h"

namespace flowcoro {
namespace memory {

/**
 * @brief 全局内存管理器
 */
class GlobalMemoryManager {
public:
    static GlobalMemoryManager& instance();
    
    template<typename T>
    std::shared_ptr<CacheFriendlyMemoryPool<T>> get_pool();
    
    void cleanup_all_pools();
    
    struct MemoryStats {
        size_t total_allocated_bytes;
        size_t total_objects;
        size_t pool_count;
        double fragmentation_ratio;
    };
    
    MemoryStats get_stats() const;
    
private:
    GlobalMemoryManager() = default;
    ~GlobalMemoryManager() = default;
};

} // namespace memory
} // namespace flowcoro
