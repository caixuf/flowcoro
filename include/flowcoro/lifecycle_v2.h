#pragma once

/**
 * @file lifecycle_v2.h
 * @brief FlowCoro 生命周期管理系统 v2.0 - Phase 4 集成版本
 * @author FlowCoro Team
 * @date 2025-07-21
 * 
 * Phase 4: 渐进式集成到主代码库
 * 整合Phase 1-3的所有成果，提供统一的协程生命周期管理接口
 */

// 核心组件 (Phase 1 & 2 成果)
#include "lifecycle/core.h"
#include "lifecycle/cancellation.h"
#include "lifecycle/enhanced_task.h"

// Phase 3 协程池组件
#include "lifecycle/coroutine_pool.h" 
#include "lifecycle/pooled_task.h"
#include "lifecycle/advanced_lifecycle.h"

// 依赖的基础设施
#include "core.h"
#include "buffer.h"
#include "object_pool.h"

// 简单的日志宏定义
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#endif

namespace flowcoro::v2 {

/**
 * @brief 统一的任务类型 - Phase 4 集成版本
 * 
 * 提供三种使用模式：
 * 1. 兼容模式：完全兼容现有Task<T>
 * 2. 增强模式：启用生命周期管理  
 * 3. 池化模式：启用协程池优化
 */
template<typename T = void>
class Task {
private:
    enum class TaskMode {
        Compatible,  // 兼容现有代码
        Enhanced,    // 增强生命周期管理
        Pooled       // 池化优化
    };
    
    TaskMode mode_;
    
    // 存储不同模式的实现
    union {
        flowcoro::Task<T> compatible_task_;
        lifecycle::enhanced_task<T> enhanced_task_;
        lifecycle::pooled_task<T> pooled_task_;
    };
    
    // 生命周期管理组件
    std::optional<lifecycle::coroutine_lifecycle_guard> lifecycle_guard_;

public:
    // 兼容构造函数 - 与现有Task<T>完全兼容
    explicit Task(flowcoro::Task<T>&& task) 
        : mode_(TaskMode::Compatible), compatible_task_(std::move(task)) {}
    
    // 增强构造函数 - 启用生命周期管理
    explicit Task(lifecycle::enhanced_task<T>&& task, bool enable_lifecycle = true)
        : mode_(TaskMode::Enhanced), enhanced_task_(std::move(task)) {
        if (enable_lifecycle) {
            // 创建生命周期守护
            lifecycle_guard_ = lifecycle::make_guarded_coroutine(
                enhanced_task_.handle(), "enhanced_task");
        }
    }
    
    // 池化构造函数 - 启用协程池优化
    explicit Task(lifecycle::pooled_task<T>&& task)
        : mode_(TaskMode::Pooled), pooled_task_(std::move(task)) {}
    
    // 移动语义
    Task(Task&& other) noexcept : mode_(other.mode_) {
        switch (mode_) {
            case TaskMode::Compatible:
                new (&compatible_task_) flowcoro::Task<T>(std::move(other.compatible_task_));
                break;
            case TaskMode::Enhanced:
                new (&enhanced_task_) lifecycle::enhanced_task<T>(std::move(other.enhanced_task_));
                lifecycle_guard_ = std::move(other.lifecycle_guard_);
                break;
            case TaskMode::Pooled:
                new (&pooled_task_) lifecycle::pooled_task<T>(std::move(other.pooled_task_));
                break;
        }
    }
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            this->~Task();
            new (this) Task(std::move(other));
        }
        return *this;
    }
    
    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    ~Task() {
        switch (mode_) {
            case TaskMode::Compatible:
                compatible_task_.~Task();
                break;
            case TaskMode::Enhanced:
                enhanced_task_.~enhanced_task();
                break;
            case TaskMode::Pooled:
                pooled_task_.~pooled_task();
                break;
        }
    }
    
    // 统一的等待接口
    bool await_ready() const noexcept {
        switch (mode_) {
            case TaskMode::Compatible: return compatible_task_.await_ready();
            case TaskMode::Enhanced: return enhanced_task_.await_ready();
            case TaskMode::Pooled: return pooled_task_.await_ready();
        }
        return false;
    }
    
    template<typename U>
    void await_suspend(std::coroutine_handle<U> handle) const {
        switch (mode_) {
            case TaskMode::Compatible: 
                compatible_task_.await_suspend(handle);
                break;
            case TaskMode::Enhanced: 
                enhanced_task_.await_suspend(handle);
                break;
            case TaskMode::Pooled: 
                pooled_task_.await_suspend(handle);
                break;
        }
    }
    
    T await_resume() {
        switch (mode_) {
            case TaskMode::Compatible: 
                if constexpr (std::is_void_v<T>) {
                    compatible_task_.await_resume();
                    return;
                } else {
                    return compatible_task_.await_resume();
                }
            case TaskMode::Enhanced: 
                return enhanced_task_.await_resume();
            case TaskMode::Pooled: 
                return pooled_task_.await_resume();
        }
    }
    
    // 扩展功能 - 仅在增强/池化模式下可用
    void set_cancellation_token(lifecycle::cancellation_token token) {
        switch (mode_) {
            case TaskMode::Enhanced:
                enhanced_task_.set_cancellation_token(std::move(token));
                break;
            case TaskMode::Pooled:
                pooled_task_.set_cancellation_token(std::move(token));
                break;
            case TaskMode::Compatible:
                // 兼容模式不支持取消，静默忽略
                break;
        }
    }
    
    void cancel() {
        switch (mode_) {
            case TaskMode::Enhanced:
                if (lifecycle_guard_) {
                    lifecycle_guard_->cancel();
                }
                break;
            case TaskMode::Pooled:
                pooled_task_.cancel();
                break;
            case TaskMode::Compatible:
                // 兼容模式不支持取消
                break;
        }
    }
    
    bool done() const noexcept {
        switch (mode_) {
            case TaskMode::Compatible: 
                // 兼容模式没有done()方法，返回false
                return false;
            case TaskMode::Enhanced: 
                return enhanced_task_.done();
            case TaskMode::Pooled: 
                return pooled_task_.done();
        }
        return false;
    }
    
    // 调试和监控接口
    std::string get_debug_info() const {
        switch (mode_) {
            case TaskMode::Compatible:
                return "Task v2: compatible mode";
            case TaskMode::Enhanced:
                return "Task v2: enhanced mode - " + enhanced_task_.get_debug_info();
            case TaskMode::Pooled:
                return "Task v2: pooled mode - " + pooled_task_.get_debug_info();
        }
        return "Task v2: unknown mode";
    }
    
    TaskMode get_mode() const noexcept { return mode_; }
    
    bool is_enhanced() const noexcept { 
        return mode_ == TaskMode::Enhanced || mode_ == TaskMode::Pooled; 
    }
    
    bool is_pooled() const noexcept { 
        return mode_ == TaskMode::Pooled; 
    }
};

/**
 * @brief 便利的任务创建函数
 */
namespace factory {
    
    // 创建兼容任务 (与现有代码兼容)
    template<typename T>
    Task<T> make_compatible_task(flowcoro::Task<T>&& task) {
        return Task<T>(std::move(task));
    }
    
    // 创建增强任务 (启用生命周期管理)
    template<typename T>
    Task<T> make_enhanced_task(lifecycle::enhanced_task<T>&& task) {
        return Task<T>(std::move(task), true);
    }
    
    // 创建池化任务 (最优性能)
    template<typename T>
    Task<T> make_pooled_task(lifecycle::pooled_task<T>&& task) {
        return Task<T>(std::move(task));
    }
    
    // 智能任务工厂 - 根据环境自动选择最佳模式
    template<typename T>
    Task<T> make_smart_task() {
        // 检查是否启用了池化
        auto& pool = lifecycle::get_global_coroutine_pool();
        auto stats = pool.get_stats();
        
        if (stats.hit_ratio() > 50.0) {
            // 池化效果良好，使用池化模式
            return make_pooled_task<T>(lifecycle::pooled_task<T>{});
        } else {
            // 使用增强模式
            return make_enhanced_task<T>(lifecycle::enhanced_task<T>{});
        }
    }
}

/**
 * @brief 全局生命周期管理器 - Phase 4 集成版本
 */
class lifecycle_manager {
private:
    // 整合所有统计信息
    struct unified_stats {
        // Phase 1&2 统计
        size_t total_coroutines{0};
        size_t active_coroutines{0};
        size_t completed_coroutines{0};
        size_t cancelled_coroutines{0};
        size_t failed_coroutines{0};
        
        // Phase 3 池化统计
        lifecycle::coroutine_pool_stats pool_stats;
        
        std::string to_string() const {
            std::ostringstream oss;
            oss << "FlowCoro v2 Lifecycle Stats:\n"
                << "  Basic Coroutines - Total: " << total_coroutines 
                << ", Active: " << active_coroutines << ", Completed: " << completed_coroutines << "\n"
                << "  Pool Stats:\n" << pool_stats.to_string();
            return oss.str();
        }
    };
    
    std::atomic<size_t> compatible_tasks_{0};
    std::atomic<size_t> enhanced_tasks_{0};
    std::atomic<size_t> pooled_tasks_{0};
    
public:
    static lifecycle_manager& instance() {
        static lifecycle_manager inst;
        return inst;
    }
    
    void on_task_created(Task<>::TaskMode mode) {
        switch (mode) {
            case Task<>::TaskMode::Compatible: compatible_tasks_.fetch_add(1); break;
            case Task<>::TaskMode::Enhanced: enhanced_tasks_.fetch_add(1); break;
            case Task<>::TaskMode::Pooled: pooled_tasks_.fetch_add(1); break;
        }
    }
    
    unified_stats get_unified_stats() const {
        unified_stats stats;
        stats.total_coroutines = compatible_tasks_ + enhanced_tasks_ + pooled_tasks_;
        stats.pool_stats = lifecycle::get_global_coroutine_pool().get_stats();
        return stats;
    }
    
    void print_integration_report() const {
        auto stats = get_unified_stats();
        
        LOG_INFO("=== FlowCoro Phase 4 Integration Report ===");
        LOG_INFO("Task Distribution:");
        LOG_INFO("  Compatible Tasks: %zu", compatible_tasks_.load());
        LOG_INFO("  Enhanced Tasks: %zu", enhanced_tasks_.load());
        LOG_INFO("  Pooled Tasks: %zu", pooled_tasks_.load());
        LOG_INFO("");
        LOG_INFO("%s", stats.to_string().c_str());
        
        // 计算迁移建议
        size_t total = stats.total_coroutines;
        if (total > 0) {
            double pooled_ratio = (double)pooled_tasks_ / total * 100.0;
            double enhanced_ratio = (double)enhanced_tasks_ / total * 100.0;
            
            LOG_INFO("Migration Analysis:");
            LOG_INFO("  Pooled Adoption: %.1f%%", pooled_ratio);
            LOG_INFO("  Enhanced Adoption: %.1f%%", enhanced_ratio);
            
            if (pooled_ratio > 70.0) {
                LOG_INFO("  ✅ Excellent pooling adoption - ready for production");
            } else if (pooled_ratio > 30.0) {
                LOG_INFO("  🔄 Good progress - continue migrating to pooled tasks");
            } else {
                LOG_INFO("  ⚠️  Low pooling adoption - consider migration strategy");
            }
        }
    }
};

/**
 * @brief 渐进式迁移助手
 */
namespace migration {
    
    // 迁移策略枚举
    enum class strategy {
        conservative,  // 保守迁移 - 只在新代码中使用增强功能
        aggressive,    // 积极迁移 - 逐步替换现有Task
        full_pooling   // 完全池化 - 所有新任务使用池化
    };
    
    class migration_helper {
    private:
        strategy current_strategy_ = strategy::conservative;
        
    public:
        void set_strategy(strategy s) {
            current_strategy_ = s;
            LOG_INFO("Migration strategy set to: %s", 
                     s == strategy::conservative ? "conservative" :
                     s == strategy::aggressive ? "aggressive" : "full_pooling");
        }
        
        template<typename T>
        Task<T> create_task_for_migration(flowcoro::Task<T>&& original) {
            switch (current_strategy_) {
                case strategy::conservative:
                    return factory::make_compatible_task(std::move(original));
                    
                case strategy::aggressive:
                    // 转换为增强任务 - 简化实现
                    return factory::make_enhanced_task<T>(
                        lifecycle::enhanced_task<T>{});
                    
                case strategy::full_pooling:
                    // 使用池化任务 - 简化实现  
                    return factory::make_pooled_task<T>(
                        lifecycle::pooled_task<T>{});
            }
            
            // 默认兼容模式
            return factory::make_compatible_task(std::move(original));
        }
        
        void analyze_migration_opportunity() const {
            auto& manager = lifecycle_manager::instance();
            auto stats = manager.get_unified_stats();
            
            LOG_INFO("=== Migration Opportunity Analysis ===");
            
            if (stats.pool_stats.hit_ratio() > 60.0) {
                LOG_INFO("✅ Pool performing excellently - recommend aggressive migration");
            } else if (stats.pool_stats.hit_ratio() > 30.0) {
                LOG_INFO("🔄 Pool showing good results - conservative migration recommended");
            } else {
                LOG_INFO("⚠️  Pool needs optimization before large-scale migration");
            }
            
            if (stats.pool_stats.memory_saved_bytes > 100000) {
                LOG_INFO("💾 Significant memory savings achieved - %zu bytes", 
                         stats.pool_stats.memory_saved_bytes);
            }
        }
    };
    
    // 全局迁移助手
    inline migration_helper& get_migration_helper() {
        static migration_helper helper;
        return helper;
    }
}

/**
 * @brief Phase 4 Quick Start API
 */
namespace quick_start {
    
    // 推荐的新任务类型 (池化优化)
    template<typename T = void>
    using RecommendedTask = Task<T>;
    
    // 创建推荐的任务
    template<typename T>
    RecommendedTask<T> make_task() {
        return factory::make_smart_task<T>();
    }
    
    // 获取统一统计信息
    inline auto get_stats() {
        return lifecycle_manager::instance().get_unified_stats();
    }
    
    // 打印集成报告
    inline void print_report() {
        lifecycle_manager::instance().print_integration_report();
    }
    
    // 设置迁移策略
    inline void set_migration_strategy(migration::strategy s) {
        migration::get_migration_helper().set_strategy(s);
    }
}

} // namespace flowcoro::v2

/**
 * @brief 向后兼容别名 
 * 确保现有代码继续工作
 */
namespace flowcoro {
    // v2命名空间的别名
    namespace lifecycle_v2 = v2;
    
    // 为了向后兼容，保留原有的lifecycle命名空间
    // 现有代码中的 flowcoro::lifecycle 继续有效
}

// 宏定义简化API使用
#define FLOWCORO_USE_V2_TASKS() using Task = flowcoro::v2::Task<void>; \
                                template<typename T> using TaskT = flowcoro::v2::Task<T>;

#define FLOWCORO_ENABLE_POOLING() flowcoro::v2::quick_start::set_migration_strategy(\
                                      flowcoro::v2::migration::strategy::full_pooling);

#define FLOWCORO_PRINT_STATS() flowcoro::v2::quick_start::print_report();
