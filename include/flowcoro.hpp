/**
 * @file flowcoro.hpp
# 
#define FLOWCORO_VERSION_MAJOR 4
#define FLOWCORO_VERSION_MINOR 0
#define FLOWCORO_VERSION_PATCH 0
#define FLOWCORO_VERSION "4.0.0"
#define FLOWCORO_VERSION_STRING "4.0.0"ief FlowCoro - C++20
 * @author FlowCoro Team
 * @version 2.2.0
 * @date 2025-07-22
 *
 * FlowCoroC++20
 * - 
 * - Task
 * - 
 * - JavaScript PromiseAPI
 * - 
 * - 
 * - 
 *
 * v2.2.0
 * - Task<T>
 * -  (is_cancelled_, is_destroyed_)
 * - 
 * - 
 * - PromiseAPI
 */

#pragma once

// 
#define FLOWCORO_VERSION_MAJOR 3
#define FLOWCORO_VERSION_MINOR 0
#define FLOWCORO_VERSION_PATCH 0
#define FLOWCORO_VERSION "3.0.0"
#define FLOWCORO_VERSION_STRING "3.0.0"

// 
#include "flowcoro/core.h"
#include "flowcoro/lockfree.h"
#include "flowcoro/thread_pool.h"
#include "flowcoro/logger.h"
#include "flowcoro/buffer.h"
#include "flowcoro/memory.h"
#include "flowcoro/network.h"
#include "flowcoro/net.h"
#include "flowcoro/http_client.h"
#include "flowcoro/simple_db.h"
#include "flowcoro/rpc.h"
#include "flowcoro/channel.h"
#include "flowcoro/yield.h"           // yield
#include "flowcoro/task_allocator.h"  // 

// #include "flowcoro/rpc.h" // RPC

/**
 * @namespace flowcoro
 * @brief FlowCoro
 */
namespace flowcoro {

/**
 * @brief FlowCoro
 * @return 
 */
inline const char* version() {
    return FLOWCORO_VERSION;
}

/**
 * @brief FlowCoro
 * @param thread_count 
 * @param log_level INFO
 * @param log_file 
 * @return 
 */
bool initialize(
    size_t thread_count = 0,
    LogLevel log_level = LogLevel::LOG_INFO,
    const std::string& log_file = ""
);

/**
 * @brief FlowCoro
 */
void shutdown();

/**
 * @brief 
 */
struct RuntimeStats {
    size_t active_coroutines;
    size_t thread_pool_size;
    size_t total_tasks_processed;
    double cpu_utilization;
    struct {
        size_t total_logs;
        size_t dropped_logs;
        double throughput_per_sec;
    } logging;
    struct {
        size_t allocated_objects;
        size_t pool_hit_rate;
        size_t memory_usage_bytes;
    } memory;
    struct {
        size_t total_tasks;
        size_t cancelled_tasks;
        size_t destroyed_tasks;
        double avg_lifetime_ms;
    } task_lifecycle; // Task
};

/**
 * @brief 
 * @return RuntimeStats
 */
RuntimeStats get_runtime_stats();

} // namespace flowcoro
