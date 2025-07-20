/**
 * @file globals.cpp
 * @brief FlowCoro 全局变量定义
 */

#include "flowcoro/core.h"
#include "flowcoro/logger.h"
#include "flowcoro/thread_pool.h"

namespace flowcoro {

// GlobalThreadPool 静态成员定义
std::atomic<bool> GlobalThreadPool::shutdown_requested_{false};
std::atomic<lockfree::ThreadPool*> GlobalThreadPool::instance_{nullptr};
std::once_flag GlobalThreadPool::init_flag_;

// GlobalLogger 静态成员定义  
std::unique_ptr<Logger> GlobalLogger::instance_;
std::once_flag GlobalLogger::init_flag_;

} // namespace flowcoro

namespace lockfree {

// WorkStealingThreadPool thread_local 变量定义
thread_local size_t WorkStealingThreadPool::worker_id_ = SIZE_MAX;

} // namespace lockfree
