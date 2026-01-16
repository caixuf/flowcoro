#pragma once
#include <functional>
#include <coroutine>
#include "performance_monitor.h"

namespace flowcoro {

// API - 
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);
void schedule_task_enhanced(std::function<void()> task);
void drive_coroutine_pool();
void print_pool_stats();
void shutdown_coroutine_pool();

// 
void print_flowcoro_stats();
SystemStats get_flowcoro_stats();

// 
namespace debug {
    // 
    void print_system_status();
    
    // 
    void check_memory_usage();
    
    // 
    bool validate_coroutine_state();
    
    // 
    void run_performance_benchmark(size_t task_count = 1000);
}

} // namespace flowcoro
