/**
 * @file globals.cpp
 * @brief FlowCoro 全局变量定义
 */

#include "flowcoro/core.h"
#include "flowcoro/logger.h"
#include "flowcoro/thread_pool.h"

namespace flowcoro {

// GlobalLogger 静态成员定义
std::unique_ptr<Logger> GlobalLogger::instance_;
std::once_flag GlobalLogger::init_flag_;

} // namespace flowcoro

namespace lockfree {

// WorkStealingThreadPool thread_local 变量定义
thread_local size_t WorkStealingThreadPool::worker_id_ = SIZE_MAX;

} // namespace lockfree
