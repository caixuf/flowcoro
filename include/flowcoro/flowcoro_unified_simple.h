#pragma once

/**
 * @file flowcoro_unified_simple.h
 * @brief FlowCoro 简化统一版本 - 不依赖外部库的完整头文件
 * @author FlowCoro Team
 * @date 2025-07-21
 */

#include "core_simple.h"
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
 * @brief 便利类型别名
 */
template<typename T = void>
using UnifiedTask = Task<T>;

template<typename T = void>
using CancellableTask = enhanced_task<T>;

/**
 * @brief 创建可取消的任务
 */
template<typename T>
auto make_cancellable(Task<T>&& task, cancellation_token token = {}) -> enhanced_task<T> {
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
