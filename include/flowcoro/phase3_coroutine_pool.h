#pragma once

/**
 * @file phase3_coroutine_pool.h
 * @brief FlowCoro Phase 3: 协程池实现
 * 
 * 这个阶段实现了高效的协程对象池，基于现有的优秀模式：
 * - ConnectionPool的池管理模式
 * - CacheFriendlyMemoryPool的内存优化
 * - ObjectPool的对象复用机制
 */

// 核心池化组件
#include "lifecycle/coroutine_pool.h"
#include "lifecycle/pooled_task.h"

// 依赖的生命周期组件  
#include "lifecycle/advanced_lifecycle.h"
#include "lifecycle/core.h"
#include "lifecycle/cancellation.h"

// 基础设施
#include "buffer.h"
#include "object_pool.h"

namespace flowcoro {
namespace phase3 {

/**
 * @brief Phase 3 功能摘要
 */
struct phase3_features {
    static constexpr const char* name = "Coroutine Pool Implementation";
    static constexpr int version_major = 1;
    static constexpr int version_minor = 0;
    
    // 功能列表
    enum class feature {
        coroutine_frame_pooling,      // 协程帧池化
        automatic_lifecycle_management, // 自动生命周期管理
        cache_friendly_allocation,    // 缓存友好分配
        pool_statistics_monitoring,   // 池统计监控
        idle_coroutine_cleanup,       // 空闲协程清理
        cancellation_integration,     // 取消机制集成
        performance_benchmarking      // 性能基准测试
    };
    
    static std::string get_description() {
        return "Advanced coroutine object pooling with lifecycle management, "
               "memory optimization, and performance monitoring. "
               "Based on ConnectionPool patterns from existing codebase.";
    }
};

/**
 * @brief 快速开始接口
 */
namespace quick_start {
    
    // 使用池化协程的简单方式
    template<typename T = void>
    using Task = lifecycle::pooled_task<T>;
    
    // 创建池化协程
    template<typename Awaitable>
    auto make_task(Awaitable&& awaitable) {
        return lifecycle::make_pooled_task(std::forward<Awaitable>(awaitable));
    }
    
    // 获取全局协程池统计
    inline auto get_pool_stats() {
        return lifecycle::get_global_coroutine_pool().get_stats();
    }
    
    // 手动清理池
    inline void cleanup_pool() {
        lifecycle::get_global_coroutine_pool().cleanup_idle_coroutines();
    }
    
    // 关闭池（通常在应用退出时调用）
    inline void shutdown_pool() {
        lifecycle::get_global_coroutine_pool().shutdown();
    }
}

/**
 * @brief 高级接口 - 用于定制和优化
 */
namespace advanced {
    
    using coroutine_pool = lifecycle::coroutine_lifecycle_pool;
    using pool_stats = lifecycle::coroutine_pool_stats;
    using pooled_guard = lifecycle::coroutine_lifecycle_guard;
    
    // 创建自定义协程池
    template<typename ConfigType>
    auto create_custom_pool(const ConfigType& config) {
        return std::make_unique<coroutine_pool>(config);
    }
    
    // 高级池化守护创建
    template<typename CoroutineType>
    auto make_custom_pooled_guard(
        std::coroutine_handle<CoroutineType> handle,
        const std::string& debug_name,
        coroutine_pool& pool) {
        return pool.acquire_pooled_guard(handle, debug_name);
    }
}

/**
 * @brief 性能监控工具
 */
namespace monitoring {
    
    class pool_monitor {
    private:
        lifecycle::coroutine_lifecycle_pool& pool_;
        std::chrono::steady_clock::time_point start_time_;
        
    public:
        explicit pool_monitor(lifecycle::coroutine_lifecycle_pool& pool)
            : pool_(pool), start_time_(std::chrono::steady_clock::now()) {}
        
        void print_stats() const {
            auto stats = pool_.get_stats();
            auto uptime = std::chrono::steady_clock::now() - start_time_;
            
            LOG_INFO("Pool Monitor Report:");
            LOG_INFO("%s", stats.to_string().c_str());
            LOG_INFO("Monitor uptime: %lld ms", 
                     std::chrono::duration_cast<std::chrono::milliseconds>(uptime).count());
        }
        
        void print_efficiency_analysis() const {
            auto stats = pool_.get_stats();
            
            LOG_INFO("Pool Efficiency Analysis:");
            LOG_INFO("  Hit ratio: %.2f%% %s", 
                     stats.hit_ratio(),
                     stats.hit_ratio() > 70.0 ? "(Excellent)" :
                     stats.hit_ratio() > 50.0 ? "(Good)" :
                     stats.hit_ratio() > 30.0 ? "(Fair)" : "(Poor)");
            
            if (stats.total_coroutines > 0) {
                double pool_utilization = (double)stats.pooled_coroutines / stats.total_coroutines * 100.0;
                LOG_INFO("  Pool utilization: %.2f%%", pool_utilization);
                
                if (pool_utilization > 80.0) {
                    LOG_INFO("  ✅ High pool utilization - excellent memory efficiency");
                } else if (pool_utilization < 20.0) {
                    LOG_INFO("  ⚠️  Low pool utilization - consider reducing pool size");
                }
            }
            
            if (stats.memory_saved_bytes > 0) {
                LOG_INFO("  Memory saved: %zu bytes (%.2f KB)", 
                         stats.memory_saved_bytes, stats.memory_saved_bytes / 1024.0);
            }
        }
    };
    
    // 全局监控器
    inline auto create_global_monitor() {
        return pool_monitor(lifecycle::get_global_coroutine_pool());
    }
}

} // namespace phase3
} // namespace flowcoro
