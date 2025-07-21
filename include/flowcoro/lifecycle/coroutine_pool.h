#pragma once
#include "../buffer.h"
#include "../object_pool.h"
#include "core.h"
#include "cancellation.h"
#include "advanced_lifecycle.h"
#include <coroutine>
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>

// 简单的日志宏定义
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG  
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_TRACE
#define LOG_TRACE(fmt, ...) printf("[TRACE] " fmt "\n", ##__VA_ARGS__)
#endif

namespace flowcoro::lifecycle {

// 前向声明
template<typename T>
class pooled_task;

template<typename Allocator>
class coroutine_frame_pool;

/**
 * @brief 协程帧的池化分配器
 * 基于现有CacheFriendlyMemoryPool模式，优化协程帧的内存分配
 */
template<typename T>
class coroutine_frame_allocator {
private:
    using Pool = CacheFriendlyMemoryPool<T>;
    static Pool& get_pool() {
        static Pool instance;
        return instance;
    }

public:
    using value_type = T;
    
    T* allocate(size_t n) {
        if (n != 1) {
            // 大型分配回退到标准分配器
            return static_cast<T*>(::operator new(n * sizeof(T)));
        }
        
        auto obj = get_pool().acquire();
        return obj.release(); // 释放unique_ptr的所有权
    }
    
    void deallocate(T* ptr, size_t n) {
        if (n != 1) {
            ::operator delete(ptr);
            return;
        }
        
        // 简化版本：大型分配直接删除
        // 实际实现中需要更复杂的池管理
        ::operator delete(ptr);
    }
    
    // 获取分配器统计信息
    static auto get_stats() {
        return get_pool().get_stats();
    }
};

/**
 * @brief 协程池统计信息
 */
struct coroutine_pool_stats {
    size_t total_coroutines{0};
    size_t active_coroutines{0};
    size_t pooled_coroutines{0};
    size_t cache_hits{0};
    size_t cache_misses{0};
    size_t memory_saved_bytes{0};
    
    // 性能指标
    double hit_ratio() const {
        auto total = cache_hits + cache_misses;
        return total > 0 ? (double)cache_hits / total * 100.0 : 0.0;
    }
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << "CoroutinePool Stats:\n"
            << "  Total: " << total_coroutines << "\n"
            << "  Active: " << active_coroutines << "\n"  
            << "  Pooled: " << pooled_coroutines << "\n"
            << "  Hit Ratio: " << std::fixed << std::setprecision(2) << hit_ratio() << "%\n"
            << "  Memory Saved: " << memory_saved_bytes << " bytes";
        return oss.str();
    }
};

/**
 * @brief 协程生命周期池化管理器
 * 基于ConnectionPool模式，为协程提供池化生命周期管理
 */
class coroutine_lifecycle_pool {
private:
    // 池化协程句柄结构
    struct pooled_coroutine_data {
        advanced_coroutine_handle handle;
        std::chrono::steady_clock::time_point created_time;
        std::chrono::steady_clock::time_point last_used_time;
        std::atomic<bool> in_use{false};
        std::string debug_name;
        size_t reuse_count{0};
        
        pooled_coroutine_data(advanced_coroutine_handle h, const std::string& name)
            : handle(std::move(h))
            , created_time(std::chrono::steady_clock::now())
            , last_used_time(created_time)
            , debug_name(name) {}
        
        void mark_used() {
            last_used_time = std::chrono::steady_clock::now();
            in_use.store(true, std::memory_order_release);
            ++reuse_count;
        }
        
        void mark_unused() {
            last_used_time = std::chrono::steady_clock::now();
            in_use.store(false, std::memory_order_release);
        }
        
        auto get_idle_time() const {
            return std::chrono::steady_clock::now() - last_used_time;
        }
        
        bool is_reusable() const {
            return !in_use.load(std::memory_order_acquire) && handle.is_valid();
        }
    };
    
    using PooledCoroPtr = std::shared_ptr<pooled_coroutine_data>;
    
    // 池配置
    struct pool_config {
        size_t max_pooled_coroutines{1000};
        size_t min_pooled_coroutines{10};
        std::chrono::seconds idle_timeout{300}; // 5分钟
        std::chrono::seconds cleanup_interval{60}; // 1分钟清理间隔
        bool enable_statistics{true};
        bool enable_reuse_validation{true};
    } config_;
    
    // 协程池状态
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_condition_;
    CacheFriendlyRingBuffer<PooledCoroPtr, 1024> available_coroutines_;
    std::unordered_map<std::coroutine_handle<>, PooledCoroPtr> all_coroutines_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    coroutine_pool_stats stats_;
    
    // 后台清理任务
    std::atomic<bool> shutdown_{false};
    std::unique_ptr<std::thread> cleanup_thread_;

public:
    explicit coroutine_lifecycle_pool(const pool_config& cfg = {})
        : config_(cfg) {
        
        // 预分配最小数量的协程槽位
        for (size_t i = 0; i < config_.min_pooled_coroutines; ++i) {
            // 这里我们创建空的槽位，实际协程会在需要时创建
        }
        
        // 启动后台清理任务
        start_cleanup_task();
        
        LOG_INFO("Coroutine pool initialized with max=%zu, min=%zu", 
                 config_.max_pooled_coroutines, config_.min_pooled_coroutines);
    }
    
    ~coroutine_lifecycle_pool() {
        shutdown();
    }
    
    /**
     * @brief 获取一个池化的协程生命周期守护
     * 类似ConnectionPool::acquire_connection的设计
     */
    template<typename CoroutineType>
    coroutine_lifecycle_guard acquire_pooled_guard(
        std::coroutine_handle<CoroutineType> handle, 
        const std::string& debug_name = "pooled_coroutine") {
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_coroutines++;
        }
        
        // 首先尝试从池中获取可复用的守护
        PooledCoroPtr pooled_coro = try_acquire_from_pool();
        
        if (pooled_coro) {
            // 缓存命中
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.cache_hits++;
                stats_.active_coroutines++;
                stats_.memory_saved_bytes += sizeof(pooled_coroutine_data);
            }
            
            // 重新初始化守护数据
            pooled_coro->handle = advanced_coroutine_handle::wrap(handle);
            pooled_coro->debug_name = debug_name;
            pooled_coro->mark_used();
            
            LOG_DEBUG("Reusing pooled coroutine guard for '%s' (reuse count: %zu)",
                      debug_name.c_str(), pooled_coro->reuse_count);
            
            return create_guard_from_pooled(pooled_coro);
        }
        
        // 缓存未命中，创建新的守护
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.cache_misses++;
            stats_.active_coroutines++;
        }
        
        auto advanced_handle = advanced_coroutine_handle::wrap(handle);
        auto pooled_data = std::make_shared<pooled_coroutine_data>(
            std::move(advanced_handle), debug_name);
        pooled_data->mark_used();
        
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            all_coroutines_[handle.address()] = pooled_data;
        }
        
        LOG_DEBUG("Created new pooled coroutine guard for '%s'", debug_name.c_str());
        
        return create_guard_from_pooled(pooled_data);
    }
    
    /**
     * @brief 归还协程到池中
     * 类似ConnectionPool::return_connection的设计
     */
    void return_to_pool(PooledCoroPtr pooled_coro) {
        if (!pooled_coro || shutdown_.load()) return;
        
        pooled_coro->mark_unused();
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.active_coroutines--;
        }
        
        // 验证协程是否还可以复用
        if (config_.enable_reuse_validation && !pooled_coro->handle.is_valid()) {
            LOG_WARN("Removing invalid coroutine from pool: %s", 
                     pooled_coro->debug_name.c_str());
            remove_from_pool(pooled_coro);
            return;
        }
        
        // 检查池是否已满
        if (!available_coroutines_.push(pooled_coro)) {
            LOG_DEBUG("Pool full, removing coroutine: %s", pooled_coro->debug_name.c_str());
            remove_from_pool(pooled_coro);
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.pooled_coroutines++;
        }
        
        LOG_DEBUG("Returned coroutine to pool: %s (pool size: %zu)",
                  pooled_coro->debug_name.c_str(), available_coroutines_.size());
        
        // 通知等待的线程
        pool_condition_.notify_one();
    }
    
    /**
     * @brief 获取池统计信息
     */
    coroutine_pool_stats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }
    
    /**
     * @brief 手动触发清理
     */
    void cleanup_idle_coroutines() {
        std::vector<PooledCoroPtr> to_remove;
        
        // 收集需要清理的协程
        PooledCoroPtr coro;
        while (available_coroutines_.pop(coro)) {
            if (coro->get_idle_time() > config_.idle_timeout) {
                to_remove.push_back(coro);
            } else {
                // 重新放回队列
                available_coroutines_.push(coro);
            }
        }
        
        // 执行清理
        size_t removed_count = 0;
        for (auto& coro : to_remove) {
            remove_from_pool(coro);
            removed_count++;
        }
        
        if (removed_count > 0) {
            LOG_INFO("Cleaned up %zu idle coroutines", removed_count);
            
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.pooled_coroutines -= removed_count;
        }
    }
    
    /**
     * @brief 关闭协程池
     */
    void shutdown() {
        if (shutdown_.exchange(true)) return;
        
        LOG_INFO("Shutting down coroutine pool...");
        
        // 停止后台任务
        if (cleanup_thread_ && cleanup_thread_->joinable()) {
            cleanup_thread_->join();
        }
        
        // 清理所有协程
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            
            PooledCoroPtr coro;
            while (available_coroutines_.pop(coro)) {
                // 协程会在析构时自动清理
            }
            
            all_coroutines_.clear();
        }
        
        LOG_INFO("Coroutine pool shutdown completed");
    }

private:
    PooledCoroPtr try_acquire_from_pool() {
        PooledCoroPtr coro;
        if (available_coroutines_.pop(coro) && coro->is_reusable()) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.pooled_coroutines--;
            return coro;
        }
        return nullptr;
    }
    
    coroutine_lifecycle_guard create_guard_from_pooled(PooledCoroPtr pooled_data) {
        // 创建一个带有池化支持的生命周期守护
        // 当守护析构时，会自动调用return_to_pool
        
        auto guard = coroutine_lifecycle_guard::create_pooled(
            pooled_data->handle, 
            pooled_data->debug_name,
            [this, pooled_data]() {
                this->return_to_pool(pooled_data);
            });
        
        return guard;
    }
    
    void remove_from_pool(PooledCoroPtr pooled_coro) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        if (pooled_coro->handle.address()) {
            all_coroutines_.erase(pooled_coro->handle.address());
        }
    }
    
    void start_cleanup_task() {
        cleanup_thread_ = std::make_unique<std::thread>([this]() {
            while (!shutdown_.load()) {
                try {
                    std::this_thread::sleep_for(config_.cleanup_interval);
                    if (shutdown_.load()) break;
                    
                    cleanup_idle_coroutines();
                    
                } catch (const std::exception& e) {
                    LOG_ERROR("Cleanup task error: %s", e.what());
                }
            }
        });
    }
};

/**
 * @brief 全局协程池实例
 */
inline coroutine_lifecycle_pool& get_global_coroutine_pool() {
    static coroutine_lifecycle_pool instance;
    return instance;
}

/**
 * @brief 便利函数：创建池化的协程生命周期守护
 */
template<typename CoroutineType>
inline coroutine_lifecycle_guard make_pooled_coroutine_guard(
    std::coroutine_handle<CoroutineType> handle,
    const std::string& debug_name = "pooled_coroutine") {
    
    return get_global_coroutine_pool().acquire_pooled_guard(handle, debug_name);
}

} // namespace flowcoro::lifecycle
