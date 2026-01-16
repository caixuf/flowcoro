#pragma once

// FlowCoro  - 
// API

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

// 
#include "result.h"
#include "error_handling.h"
#include "lockfree.h"
#include "memory_pool.h"
#include "thread_pool.h"
#include "buffer.h"
#include "logger.h"

// HttpRequest
class HttpRequest;

// FlowCoro
#include "performance_monitor.h"     // 
#include "load_balancer.h"          // 
#include "coroutine_state.h"        // 
#include "safe_handle.h"            // 
#include "thread_pool_wrapper.h"    // 
#include "coroutine_manager.h"      // 
#include "awaiter.h"                // awaiter
#include "coroutine_scope.h"        // RAII
#include "network_request.h"        // 
#include "coro_task.h"              // CoroTask
#include "task.h"                   // Task
#include "async_promise.h"          // Promise
#include "when_any.h"               // when_any/when_all
#include "sync_wait.h"              // 
#include "scheduler_api.h"          // API

namespace flowcoro {

// ==========================================
//  - 
// ==========================================

//  - 
template<typename Rep, typename Period>
inline auto sleep_for(std::chrono::duration<Rep, Period> duration) {
    return ClockAwaiter(duration);
}

// 
using EnhancedSleepAwaiter = ClockAwaiter;

} // namespace flowcoro
