/**
 * @brief 高级生命周期管理演示
 * 展示基于现有代码模式改进的协程生命周期管理
 */

#include "flowcoro/lifecycle/advanced_lifecycle.h"
#include "flowcoro/core.h"
#include <iostream>
#include <vector>
#include <thread>

using namespace flowcoro;
using namespace flowcoro::lifecycle;
using namespace std::chrono_literals;

// 演示1: 引用计数句柄管理
Task<void> demo_ref_counted_handles() {
    LOG_INFO("=== 演示1: 引用计数句柄管理 ===");
    
    // 创建一个协程句柄
    auto original_task = []() -> Task<int> {
        LOG_INFO("Original task starting");
        std::this_thread::sleep_for(100ms);
        co_return 42;
    }();
    
    // 通过构造函数获取句柄地址 (这里简化演示)
    // 实际使用中，advanced_coroutine_handle应该从Task内部获取
    advanced_coroutine_handle handle1{std::coroutine_handle<>::from_address(nullptr)};
    
    {
        // 创建引用计数的拷贝
        auto handle2 = handle1;
        auto handle3 = handle1;
        
        LOG_INFO("Handle ref count: %d", handle1.ref_count());
        
        // handle2和handle3离开作用域，引用计数应该递减
    }
    
    LOG_INFO("Handle ref count after scope: %d", handle1.ref_count());
    
    co_return;
}

// 演示2: RAII生命周期守护
Task<void> demo_lifecycle_guard() {
    LOG_INFO("=== 演示2: RAII生命周期守护 ===");
    
    {
        // 创建生命周期守护
        auto guard = make_guarded_coroutine(
            std::coroutine_handle<>::from_address(nullptr),  // 简化演示
            "demo_coroutine"
        );
        
        LOG_INFO("Coroutine created: %s", guard.get_debug_info().c_str());
        
        // 模拟一些工作
        std::this_thread::sleep_for(200ms);
        
        LOG_INFO("Coroutine running: %s", guard.get_debug_info().c_str());
        
        // guard离开作用域时会自动清理
    }
    
    LOG_INFO("Coroutine guard destroyed");
    
    co_return;
}

// 演示3: 高级超时管理
Task<void> demo_advanced_timeout() {
    LOG_INFO("=== 演示3: 高级超时管理 ===");
    
    // 创建一个长时间运行的协程
    auto guard = make_guarded_coroutine(
        std::coroutine_handle<>::from_address(nullptr),
        "long_running_operation"
    );
    
    // 设置超时管理
    auto timeout_request = with_advanced_timeout(
        guard, 
        500ms,
        "database_query"
    );
    
    LOG_INFO("Registered timeout for operation");
    
    // 模拟操作
    std::this_thread::sleep_for(300ms);
    
    if (guard.get_state() == coroutine_state::cancelled) {
        LOG_INFO("Operation was cancelled due to timeout");
    } else {
        LOG_INFO("Operation completed within timeout");
    }
    
    // 获取超时管理器统计
    auto stats = advanced_timeout_manager::get().get_stats();
    LOG_INFO("Timeout manager stats: active=%zu", stats.active_timeouts);
    
    co_return;
}

// 演示4: 多协程并发管理
Task<void> demo_concurrent_lifecycle_management() {
    LOG_INFO("=== 演示4: 多协程并发管理 ===");
    
    std::vector<coroutine_lifecycle_guard> guards;
    
    // 创建多个协程守护
    for (int i = 0; i < 5; ++i) {
        auto guard = make_guarded_coroutine(
            std::coroutine_handle<>::from_address(nullptr),
            "concurrent_task_" + std::to_string(i)
        );
        
        guards.push_back(std::move(guard));
    }
    
    LOG_INFO("Created %zu concurrent coroutines", guards.size());
    
    // 模拟并发工作
    std::vector<std::thread> workers;
    for (size_t i = 0; i < guards.size(); ++i) {
        workers.emplace_back([&guard = guards[i], i]() {
            std::this_thread::sleep_for(100ms * (i + 1));
            
            LOG_INFO("Worker %zu debug info: %s", i, guard.get_debug_info().c_str());
        });
    }
    
    // 等待所有工作完成
    for (auto& worker : workers) {
        worker.join();
    }
    
    LOG_INFO("All concurrent operations completed");
    
    co_return;
}

// 演示5: 协程取消传播
Task<void> demo_cancellation_propagation() {
    LOG_INFO("=== 演示5: 协程取消传播 ===");
    
    cancellation_source master_cancel;
    auto master_token = master_cancel.get_token();
    
    // 创建多个相关协程
    std::vector<coroutine_lifecycle_guard> related_coroutines;
    
    for (int i = 0; i < 3; ++i) {
        auto guard = make_guarded_coroutine(
            std::coroutine_handle<>::from_address(nullptr),
            "related_task_" + std::to_string(i)
        );
        
        guard.set_cancellation_token(master_token);
        related_coroutines.push_back(std::move(guard));
    }
    
    // 模拟工作一段时间后取消
    std::this_thread::sleep_for(100ms);
    
    LOG_INFO("Requesting master cancellation...");
    master_cancel.cancel();
    
    // 检查所有协程是否被取消
    std::this_thread::sleep_for(50ms);
    
    int cancelled_count = 0;
    for (const auto& guard : related_coroutines) {
        if (guard.get_state() == coroutine_state::cancelled) {
            cancelled_count++;
        }
    }
    
    LOG_INFO("Cancelled %d out of %zu coroutines", 
             cancelled_count, related_coroutines.size());
    
    co_return;
}

Task<void> run_advanced_demos() {
    LOG_INFO("FlowCoro高级协程生命周期管理演示开始");
    
    co_await demo_ref_counted_handles();
    co_await demo_lifecycle_guard();
    co_await demo_advanced_timeout();
    co_await demo_concurrent_lifecycle_management();
    co_await demo_cancellation_propagation();
    
    LOG_INFO("所有高级演示完成！");
}

int main() {
    try {
        LOG_INFO("Starting FlowCoro Advanced Lifecycle Demo");
        
        // 获取初始统计
        auto initial_stats = get_coroutine_stats();
        LOG_INFO("Initial stats: total=%zu, active=%zu", 
                initial_stats.total_created, initial_stats.active_coroutines);
        
        // 运行演示
        auto demo_task = run_advanced_demos();
        demo_task.get();
        
        // 获取最终统计
        auto final_stats = get_coroutine_stats();
        LOG_INFO("Final stats: total=%zu, active=%zu, completed=%zu", 
                final_stats.total_created, 
                final_stats.active_coroutines,
                final_stats.completed_coroutines);
        
        // 获取超时管理器统计
        auto timeout_stats = advanced_timeout_manager::get().get_stats();
        LOG_INFO("Timeout manager stats: active=%zu", timeout_stats.active_timeouts);
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "高级生命周期管理演示总结:\n";
        std::cout << "- 展示了引用计数句柄管理\n";
        std::cout << "- 演示了RAII生命周期守护\n"; 
        std::cout << "- 测试了高级超时管理\n";
        std::cout << "- 验证了并发协程管理\n";
        std::cout << "- 展示了取消传播机制\n";
        std::cout << "- 协程成功率: " << std::fixed << std::setprecision(1) 
                  << (final_stats.completion_rate * 100) << "%\n";
        
        LOG_INFO("Advanced demo completed successfully!");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Advanced demo failed: %s", e.what());
        return 1;
    }
}
