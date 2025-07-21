/**
 * @file simple_lifecycle_demo.cpp
 * @brief 简化的协程生命周期管理演示
 * @author FlowCoro Team  
 * @date 2025-07-21
 */

#include "flowcoro/lifecycle.h"
#include "flowcoro/core.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace flowcoro;
using namespace flowcoro::lifecycle;
using namespace std::chrono_literals;

// 简单的增强任务演示
enhanced_task<int> simple_computation(int value) {
    coroutine_guard guard;  // 自动生命周期管理
    
    LOG_INFO("simple_computation: Starting computation with value %d", value);
    std::this_thread::sleep_for(100ms);  // 模拟工作
    
    int result = value * 2;
    LOG_INFO("simple_computation: Completed with result %d", result);
    
    co_return result;
}

// 测试取消功能的任务
enhanced_task<std::string> cancellable_computation() {
    coroutine_guard guard;
    
    LOG_INFO("cancellable_computation: Starting long computation");
    
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(100ms);
        LOG_INFO("cancellable_computation: Step %d/10", i + 1);
    }
    
    LOG_INFO("cancellable_computation: Completed");
    co_return "Task completed";
}

Task<void> demo_basic_enhanced_task() {
    LOG_INFO("=== 演示1: 基础增强任务功能 ===");
    
    // 创建一个简单任务
    auto task = simple_computation(21);
    
    // 获取初始状态
    LOG_INFO("Task state: %s", state_name(task.get_state()));
    LOG_INFO("Task lifetime: %lld ms", task.get_lifetime().count());
    
    // 等待结果
    auto result = co_await task;
    
    LOG_INFO("Task result: %d", result);
    LOG_INFO("Final task state: %s", state_name(task.get_state()));
    LOG_INFO("Final lifetime: %lld ms", task.get_lifetime().count());
}

Task<void> demo_cancellation() {
    LOG_INFO("=== 演示2: 取消机制 ===");
    
    // 创建取消源和令牌
    cancellation_source cancel_source;
    auto cancel_token = cancel_source.get_token();
    
    LOG_INFO("Token initially cancelled: %s", cancel_token.is_cancelled() ? "true" : "false");
    
    // 创建可取消的任务
    auto task = cancellable_computation();
    task.set_cancellation_token(cancel_token);
    
    // 在短时间后取消
    std::thread canceller([&cancel_source]() {
        std::this_thread::sleep_for(350ms);  // 让任务运行几步
        LOG_INFO("Requesting cancellation...");
        cancel_source.cancel();
    });
    
    try {
        auto result = co_await task;
        LOG_INFO("Task result: %s", result.c_str());
    } catch (const operation_cancelled_exception& e) {
        LOG_INFO("Task was cancelled: %s", e.what());
    } catch (const std::exception& e) {
        LOG_INFO("Task failed with exception: %s", e.what());
    }
    
    canceller.join();
    LOG_INFO("Cancellation demo completed");
}

Task<void> demo_statistics() {
    LOG_INFO("=== 演示3: 统计信息 ===");
    
    // 获取统计信息
    auto stats = get_coroutine_stats();
    
    LOG_INFO("Current statistics:");
    LOG_INFO("  Active coroutines: %zu", stats.active_coroutines);
    LOG_INFO("  Total created: %zu", stats.total_created);
    LOG_INFO("  Completed: %zu", stats.completed_coroutines);
    LOG_INFO("  Cancelled: %zu", stats.cancelled_coroutines);
    LOG_INFO("  Failed: %zu", stats.failed_coroutines);
    LOG_INFO("  Completion rate: %.1f%%", stats.completion_rate * 100);
    
    co_return;
}

Task<void> run_simple_demo() {
    LOG_INFO("FlowCoro协程生命周期管理简化演示开始");
    
    co_await demo_basic_enhanced_task();
    co_await demo_cancellation();  
    co_await demo_statistics();
    
    LOG_INFO("简化演示完成！");
}

int main() {
    try {
        LOG_INFO("Starting FlowCoro Simple Lifecycle Demo");
        
        // 运行演示
        auto demo_task = run_simple_demo();
        demo_task.get();  
        
        // 最终统计
        auto final_stats = get_coroutine_stats();
        
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "演示总结:\n";
        std::cout << "- 总共创建了 " << final_stats.total_created << " 个协程\n";
        std::cout << "- 成功完成 " << final_stats.completed_coroutines << " 个\n";  
        std::cout << "- 被取消 " << final_stats.cancelled_coroutines << " 个\n";
        std::cout << "- 执行失败 " << final_stats.failed_coroutines << " 个\n";
        std::cout << "- 成功率: " << std::fixed << std::setprecision(1) 
                  << (final_stats.completion_rate * 100) << "%\n";
        
        LOG_INFO("Demo completed successfully!");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Demo failed with exception: %s", e.what());
        return 1;
    } catch (...) {
        LOG_ERROR("Demo failed with unknown exception");
        return 1;
    }
}
