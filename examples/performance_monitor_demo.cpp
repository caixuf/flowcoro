/**
 * @file performance_monitor_demo.cpp
 * @brief FlowCoro 
 */

#include "flowcoro.hpp"
#include <iostream>
#include <vector>
#include <chrono>

using namespace flowcoro;

// CPU
Task<int> cpu_task(int id) {
    // CPU
    int result = 0;
    for (int i = 0; i < 1000; ++i) {
        result += i * id;
    }
    co_return result;
}

// IO
Task<std::string> io_task(int id) {
    // IO
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return "Task " + std::to_string(id) + " completed";
}

// 
Task<double> mixed_task(int id) {
    // CPU
    auto cpu_result = co_await cpu_task(id);
    
    // IO
    co_await sleep_for(std::chrono::milliseconds(5));
    
    // 
    double result = static_cast<double>(cpu_result) / 1000.0;
    co_return result;
}

// 
void demo_basic_monitoring() {
    std::cout << "\n===  ===\n";
    
    // 
    auto initial_stats = get_flowcoro_stats();
    std::cout << ": " << initial_stats.tasks_created << "\n";
    
    // 
    std::vector<Task<int>> tasks;
    tasks.reserve(100);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        tasks.emplace_back(cpu_task(i));
    }
    
    // 
    for (auto& task : tasks) {
        auto result = sync_wait(std::move(task));
        // ...
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 
    auto final_stats = get_flowcoro_stats();
    
    std::cout << ": " << final_stats.tasks_created << "\n";
    std::cout << ": " << final_stats.tasks_completed << "\n";
    std::cout << ": " << final_stats.tasks_failed << "\n";
    std::cout << ": " << final_stats.tasks_cancelled << "\n";
    std::cout << ": " << (final_stats.task_completion_rate * 100.0) << "%\n";
    std::cout << ": " << final_stats.tasks_per_second << " /\n";
    std::cout << ": " << duration.count() << " ms\n";
}

// 
Task<void> demo_concurrent_monitoring() {
    std::cout << "\n===  ===\n";
    
    auto start_stats = get_flowcoro_stats();
    auto start_time = std::chrono::steady_clock::now();
    
    // 
    std::vector<Task<double>> mixed_tasks;
    std::vector<Task<std::string>> io_tasks;
    
    // 50
    for (int i = 0; i < 50; ++i) {
        mixed_tasks.emplace_back(mixed_task(i));
    }
    
    // 50IO
    for (int i = 0; i < 50; ++i) {
        io_tasks.emplace_back(io_task(i));
    }
    
    // 
    for (auto& task : mixed_tasks) {
        auto result = co_await task;
        // ...
    }
    
    for (auto& task : io_tasks) {
        auto result = co_await task;
        // ...
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto end_stats = get_flowcoro_stats();
    
    std::cout << "\n";
    std::cout << ": " << (end_stats.tasks_created - start_stats.tasks_created) << "\n";
    std::cout << ": " << (end_stats.tasks_completed - start_stats.tasks_completed) << "\n";
    std::cout << ": " << end_stats.scheduler_invocations << "\n";
    std::cout << ": " << end_stats.timer_events << "\n";
    std::cout << ": " << duration.count() << " ms\n";
}

//   
void demo_realtime_monitoring() {
    std::cout << "\n===  ===\n";
    
    // 
    std::cout << "100CPU...\n";
    
    // 
    auto start_stats = get_flowcoro_stats();
    auto start_time = std::chrono::steady_clock::now();
    
    // 
    std::vector<Task<int>> tasks;
    tasks.reserve(100);
    
    for (int i = 0; i < 100; ++i) {
        tasks.emplace_back(cpu_task(i));
    }
    
    // 
    for (size_t i = 0; i < tasks.size(); i += 20) {
        // 
        for (size_t j = i; j < std::min(i + 20, tasks.size()); ++j) {
            auto result = sync_wait(std::move(tasks[j]));
            (void)result; // 
        }
        
        // 
        auto current_stats = get_flowcoro_stats();
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
        
        std::cout << ": " << (i + 20) << "/100, "
                  << ": " << (current_stats.tasks_completed - start_stats.tasks_completed) 
                  << ", : " << elapsed.count() << "ms\n";
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto end_stats = get_flowcoro_stats();
    
    std::cout << ": " << total_duration.count() << "ms\n";
    std::cout << ": +" << (end_stats.tasks_created - start_stats.tasks_created) << "\n";
}

int main() {
    std::cout << "FlowCoro \n";
    std::cout << "============================\n";
    
    try {
        // 1: 
        demo_basic_monitoring();
        
        // 2: 
        auto concurrent_task = demo_concurrent_monitoring();
        sync_wait(std::move(concurrent_task));
        
        // 3: 
        demo_realtime_monitoring();
        
        // 
        std::cout << "\n===  ===\n";
        print_flowcoro_stats();
        
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n\n";
    return 0;
}
