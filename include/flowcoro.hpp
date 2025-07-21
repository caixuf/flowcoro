/**
 * @file flowcoro.hpp
 * @brief FlowCoro - 现代C++20协程库主头文件
 * @author FlowCoro Team
 * @version 2.0.0
 * @date 2025-07-10
 * 
 * FlowCoro是一个基于C++20协程的现代异步编程库，提供：
 * - 高性能无锁协程调度
 * - 缓存友好的数据结构
 * - 异步网络请求支持
 * - 高性能日志系统
 * - 内存池和对象池
 */

#pragma once

// 版本信息
#define FLOWCORO_VERSION_MAJOR 2
#define FLOWCORO_VERSION_MINOR 1
#define FLOWCORO_VERSION_PATCH 0
#define FLOWCORO_VERSION "2.1.0"

// 核心组件
#include "flowcoro/core.h"
#include "flowcoro/lockfree.h"
#include "flowcoro/thread_pool.h"
#include "flowcoro/logger.h"
#include "flowcoro/buffer.h"
#include "flowcoro/memory.h"
#include "flowcoro/network.h"
#include "flowcoro/net.h"
#include "flowcoro/http_client.h"

// #include "flowcoro/rpc.h"  // 暂时注释掉复杂的RPC实现

/**
 * @namespace flowcoro
 * @brief FlowCoro库的主命名空间
 */
namespace flowcoro {

/**
 * @brief 获取FlowCoro版本信息
 * @return 版本字符串
 */
inline const char* version() {
    return FLOWCORO_VERSION;
}

/**
 * @brief 初始化FlowCoro库
 * @param thread_count 线程池大小，默认为硬件并发数
 * @param log_level 日志级别，默认为INFO
 * @param log_file 日志文件路径，空字符串表示输出到控制台
 * @return 是否成功初始化
 */
bool initialize(
    size_t thread_count = 0,
    LogLevel log_level = LogLevel::LOG_INFO,
    const std::string& log_file = ""
);

/**
 * @brief 关闭FlowCoro库并清理资源
 */
void shutdown();

/**
 * @brief 获取运行时统计信息
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
};

/**
 * @brief 获取运行时统计信息
 * @return RuntimeStats结构体
 */
RuntimeStats get_runtime_stats();

} // namespace flowcoro
