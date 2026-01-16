/**
 * @file globals.cpp
 * @brief FlowCoro 
 */

#include "flowcoro/core.h"
#include "flowcoro/logger.h"
#include "flowcoro/thread_pool.h"

namespace flowcoro {

// GlobalLogger 
std::unique_ptr<Logger> GlobalLogger::instance_;
std::once_flag GlobalLogger::init_flag_;

} // namespace flowcoro

// 
namespace flowcoro {
void print_flowcoro_stats() {
    PerformanceMonitor::get_instance().print_stats();
}

SystemStats get_flowcoro_stats() {
    auto stats = PerformanceMonitor::get_instance().get_stats();
    return SystemStats{
        .tasks_created = stats.tasks_created,
        .tasks_completed = stats.tasks_completed,
        .tasks_cancelled = stats.tasks_cancelled,
        .tasks_failed = stats.tasks_failed,
        .scheduler_invocations = stats.scheduler_invocations,
        .timer_events = stats.timer_events,
        .uptime_ms = stats.uptime_ms,
        .task_completion_rate = stats.task_completion_rate,
        .tasks_per_second = stats.tasks_per_second
    };
}
} // namespace flowcoro

namespace lockfree {

// WorkStealingThreadPool thread_local 
thread_local size_t WorkStealingThreadPool::worker_id_ = SIZE_MAX;

} // namespace lockfree
