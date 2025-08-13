#pragma once
#include <functional>
#include <coroutine>
#include "performance_monitor.h"

namespace flowcoro {

// 调度器API接口 - 对外暴露的核心接口
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);
void schedule_task_enhanced(std::function<void()> task);
void drive_coroutine_pool();
void print_pool_stats();
void shutdown_coroutine_pool();

// 性能监控接口
void print_flowcoro_stats();
SystemStats get_flowcoro_stats();

// 调试和诊断工具
namespace debug {
    // 打印系统状态
    void print_system_status();
    
    // 检查内存使用情况
    void check_memory_usage();
    
    // 验证协程状态一致性
    bool validate_coroutine_state();
    
    // 性能基准测试
    void run_performance_benchmark(size_t task_count = 1000);
}

} // namespace flowcoro
