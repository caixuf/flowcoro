#pragma once

// FlowCoro 协程库 - 模块化核心头文件
// 重构后的清晰模块架构，保持API兼容性

#include <coroutine>
#include <exception>
#include <memory>
#include <utility>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <future>
#include <tuple>
#include <optional>
#include <type_traits>
#include <variant>
#include <any>
#include <queue>
#include <condition_variable>
#include <iostream>

// 基础依赖
#include "result.h"
#include "error_handling.h"
#include "lockfree.h"
#include "memory_pool.h"
#include "thread_pool.h"
#include "buffer.h"
#include "logger.h"

// 前向声明HttpRequest类（保持兼容性）
class HttpRequest;

// FlowCoro核心模块
#include "performance_monitor.h"     // 性能监控系统
#include "load_balancer.h"          // 智能负载均衡器
#include "coroutine_state.h"        // 协程状态管理
#include "safe_handle.h"            // 安全协程句柄
#include "thread_pool_wrapper.h"    // 线程池包装器
#include "coroutine_manager.h"      // 协程管理器
#include "awaiter.h"                // 各种awaiter实现
#include "coroutine_scope.h"        // RAII协程范围管理
#include "network_request.h"        // 网络请求抽象接口
#include "coro_task.h"              // CoroTask实现
#include "task.h"                   // Task模板及特化
#include "async_promise.h"          // 异步Promise实现
#include "when_any.h"               // when_any/when_all实现
#include "sync_wait.h"              // 同步等待函数
#include "scheduler_api.h"          // 调度器API接口

namespace flowcoro {

// ==========================================
// 便利函数 - 保持向后兼容性
// ==========================================

// 创建一个简单的睡眠等待器 - 支持任意时间类型
template<typename Rep, typename Period>
inline auto sleep_for(std::chrono::duration<Rep, Period> duration) {
    return ClockAwaiter(duration);
}

// 兼容性别名
using EnhancedSleepAwaiter = ClockAwaiter;

} // namespace flowcoro
