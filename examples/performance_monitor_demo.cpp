/**
 * @file performance_monitor_demo.cpp
 * @brief FlowCoro 性能监控系统演示
 */

#include "flowcoro.hpp"
#include <iostream>
#include <vector>
#include <chrono>

using namespace flowcoro;

// 简单的CPU任务
Task<int> cpu_task(int id) {
    // 模拟一些CPU工作
    int result = 0;
    for (int i = 0; i < 1000; ++i) {
        result += i * id;
    }
    co_return result;
}

// IO模拟任务
Task<std::string> io_task(int id) {
    // 模拟IO等待
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return "Task " + std::to_string(id) + " completed";
}

// 混合任务
Task<double> mixed_task(int id) {
    // 先做一些CPU工作
    auto cpu_result = co_await cpu_task(id);
    
    // 然后模拟IO
    co_await sleep_for(std::chrono::milliseconds(5));
    
    // 再做一些计算
    double result = static_cast<double>(cpu_result) / 1000.0;
    co_return result;
}

// 演示基本性能监控
void demo_basic_monitoring() {
    std::cout << "\n=== 基本性能监控演示 ===\n";
    
    // 获取初始统计
    auto initial_stats = get_flowcoro_stats();
    std::cout << "初始任务数: " << initial_stats.tasks_created << "\n";
    
    // 创建一些任务
    std::vector<Task<int>> tasks;
    tasks.reserve(100);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        tasks.emplace_back(cpu_task(i));
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        auto result = sync_wait(std::move(task));
        // 处理结果...
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 获取最终统计
    auto final_stats = get_flowcoro_stats();
    
    std::cout << "完成后任务数: " << final_stats.tasks_created << "\n";
    std::cout << "已完成任务: " << final_stats.tasks_completed << "\n";
    std::cout << "失败任务: " << final_stats.tasks_failed << "\n";
    std::cout << "取消任务: " << final_stats.tasks_cancelled << "\n";
    std::cout << "完成率: " << (final_stats.task_completion_rate * 100.0) << "%\n";
    std::cout << "吞吐量: " << final_stats.tasks_per_second << " 任务/秒\n";
    std::cout << "实际耗时: " << duration.count() << " ms\n";
}

// 演示并发性能监控
Task<void> demo_concurrent_monitoring() {
    std::cout << "\n=== 并发性能监控演示 ===\n";
    
    auto start_stats = get_flowcoro_stats();
    auto start_time = std::chrono::steady_clock::now();
    
    // 创建混合任务
    std::vector<Task<double>> mixed_tasks;
    std::vector<Task<std::string>> io_tasks;
    
    // 50个混合任务
    for (int i = 0; i < 50; ++i) {
        mixed_tasks.emplace_back(mixed_task(i));
    }
    
    // 50个IO任务
    for (int i = 0; i < 50; ++i) {
        io_tasks.emplace_back(io_task(i));
    }
    
    // 并发等待所有任务
    for (auto& task : mixed_tasks) {
        auto result = co_await task;
        // 处理结果...
    }
    
    for (auto& task : io_tasks) {
        auto result = co_await task;
        // 处理结果...
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto end_stats = get_flowcoro_stats();
    
    std::cout << "并发任务完成！\n";
    std::cout << "新增任务: " << (end_stats.tasks_created - start_stats.tasks_created) << "\n";
    std::cout << "新完成任务: " << (end_stats.tasks_completed - start_stats.tasks_completed) << "\n";
    std::cout << "总调度次数: " << end_stats.scheduler_invocations << "\n";
    std::cout << "定时器事件: " << end_stats.timer_events << "\n";
    std::cout << "并发执行时间: " << duration.count() << " ms\n";
}

// 演示实时监控  
void demo_realtime_monitoring() {
    std::cout << "\n=== 实时监控演示 ===\n";
    
    // 创建工作任务并监控
    std::cout << "创建100个CPU任务进行实时监控...\n";
    
    // 记录开始统计
    auto start_stats = get_flowcoro_stats();
    auto start_time = std::chrono::steady_clock::now();
    
    // 创建并执行任务
    std::vector<Task<int>> tasks;
    tasks.reserve(100);
    
    for (int i = 0; i < 100; ++i) {
        tasks.emplace_back(cpu_task(i));
    }
    
    // 分批等待并打印进度
    for (size_t i = 0; i < tasks.size(); i += 20) {
        // 等待这批任务
        for (size_t j = i; j < std::min(i + 20, tasks.size()); ++j) {
            auto result = sync_wait(std::move(tasks[j]));
            (void)result; // 避免警告
        }
        
        // 打印当前统计
        auto current_stats = get_flowcoro_stats();
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
        
        std::cout << "处理进度: " << (i + 20) << "/100, "
                  << "已完成: " << (current_stats.tasks_completed - start_stats.tasks_completed) 
                  << ", 耗时: " << elapsed.count() << "ms\n";
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto end_stats = get_flowcoro_stats();
    
    std::cout << "实时监控完成！总耗时: " << total_duration.count() << "ms\n";
    std::cout << "任务统计变化: +" << (end_stats.tasks_created - start_stats.tasks_created) << "\n";
}

int main() {
    std::cout << "FlowCoro 性能监控系统演示\n";
    std::cout << "============================\n";
    
    try {
        // 演示1: 基本监控
        demo_basic_monitoring();
        
        // 演示2: 并发监控（简化版）
        auto concurrent_task = demo_concurrent_monitoring();
        sync_wait(std::move(concurrent_task));
        
        // 演示3: 实时监控
        demo_realtime_monitoring();
        
        // 最终统计报告
        std::cout << "\n=== 最终统计报告 ===\n";
        print_flowcoro_stats();
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n性能监控演示完成！\n";
    return 0;
}
