#pragma once

/**
 * @file flowcoro_unified.h
 * @brief FlowCoro 统一版本 - 包含所有功能的单一头文件
 * @author FlowCoro Team
 * @date 2025-07-21
 * 
 * 这个头文件提供了FlowCoro的完整功能集，包括：
 * - 核心协程支持 (core.h)
 * - 取消令牌功能 (flowcoro_cancellation.h)
 * - 增强Task功能 (flowcoro_enhanced_task.h)
 * 
 * 使用方法：
 * #include "flowcoro/flowcoro_unified.h"
 * 
 * 或者按需包含：
 * #include "flowcoro/core.h"                   // 基础功能
 * #include "flowcoro/flowcoro_cancellation.h"  // 可选：取消功能
 * #include "flowcoro/flowcoro_enhanced_task.h" // 可选：增强Task
 */

// 核心功能
#include "core.h"

// 增强功能
#include "flowcoro_cancellation.h"
#include "flowcoro_enhanced_task.h"

namespace flowcoro {

/**
 * @brief 启用FlowCoro统一版本的所有功能
 */
inline void enable_unified_features() {
    LOG_INFO("🚀 FlowCoro Unified Version Enabled");
    LOG_INFO("   ✅ Core coroutine support");
    LOG_INFO("   ✅ Cancellation tokens"); 
    LOG_INFO("   ✅ Enhanced Task functionality");
    LOG_INFO("   ✅ Safe sleep_for implementation");
    LOG_INFO("   ✅ Async promise support");
}

/**
 * @brief 便利类型别名 - 统一版本
 */
template<typename T = void>
using UnifiedTask = Task<T>;

// 如果需要增强功能，可以使用enhanced_task
template<typename T = void>
using CancellableTask = enhanced_task<T>;

/**
 * @brief 创建可取消的任务
 */
template<typename T>
auto make_cancellable(Task<T>&& task, cancellation_token token = {}) -> enhanced_task<T> {
    // 这里需要实现转换逻辑
    // 暂时返回原任务
    return enhanced_task<T>::from_task(std::move(task), token);
}

/**
 * @brief 创建带超时的任务
 */
template<typename T>
auto with_timeout(Task<T>&& task, std::chrono::milliseconds timeout) -> enhanced_task<T> {
    auto token = cancellation_token::create_timeout(timeout);
    return make_cancellable(std::move(task), token);
}

/**
 * @brief 统一版本的性能报告
 */
inline void print_unified_report() {
    LOG_INFO("=== FlowCoro Unified Version Report ===");
    LOG_INFO("✅ Core functionality: READY");
    LOG_INFO("✅ Cancellation support: AVAILABLE");
    LOG_INFO("✅ Enhanced tasks: ENABLED");
    LOG_INFO("✅ Safe coroutine handles: ACTIVE");
}

} // namespace flowcoro
