#include <iostream>
#include <chrono>
#include <thread>
#include "../include/flowcoro/flowcoro_unified_simple.h"

using namespace flowcoro;
using namespace std::chrono_literals;

// 基本协程任务
Task<int> basic_task() {
    std::cout << "Basic task started" << std::endl;
    co_await sleep_for(100ms);
    std::cout << "Basic task completed" << std::endl;
    co_return 42;
}

// 可取消的协程任务
Task<std::string> cancellable_task(cancellation_token token) {
    std::cout << "Cancellable task started" << std::endl;
    
    for (int i = 0; i < 10; ++i) {
        // 检查是否被取消
        if (token.is_cancelled()) {
            std::cout << "Task cancelled at step " << i << std::endl;
            co_return "Cancelled";
        }
        
        std::cout << "Processing step " << i << std::endl;
        co_await sleep_for(200ms);
    }
    
    std::cout << "Cancellable task completed normally" << std::endl;
    co_return "Completed";
}

// 使用超时令牌的任务
Task<int> timeout_task() {
    // 创建3秒超时的令牌
    auto timeout_token = cancellation_token::create_timeout(3000ms);
    
    try {
        // 使用超时令牌执行可能长时间运行的任务
        auto result = co_await cancellable_task(timeout_token);
        std::cout << "Result: " << result << std::endl;
        co_return 1;
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        co_return 0;
    }
}

// 演示协程状态监控
Task<void> monitor_task(Task<std::string>& task) {
    while (!task.is_ready()) {
        auto state = task.get_state();
        auto lifetime = task.get_lifetime();
        
        std::cout << "Task state: " << static_cast<int>(state) 
                  << ", lifetime: " << lifetime.count() << "ms" << std::endl;
        
        co_await sleep_for(500ms);
    }
    
    std::cout << "Task monitoring completed" << std::endl;
}

// 主函数
int main() {
    std::cout << "===== FlowCoro 2.1 整合版示例 =====" << std::endl;
    
    // 启用统一特性
    enable_unified_features();
    
    // 示例1: 基本任务
    std::cout << "\n--- 示例1: 基本任务 ---" << std::endl;
    auto result1 = sync_wait(basic_task());
    std::cout << "Basic task result: " << result1 << std::endl;
    
    // 示例2: 带监控的取消任务
    std::cout << "\n--- 示例2: 带监控的取消任务 ---" << std::endl;
    cancellation_source source;
    auto token = source.get_token();
    auto task = cancellable_task(token);
    
    // 启动监控
    auto monitor = monitor_task(task);
    
    // 等待一段时间后取消
    std::this_thread::sleep_for(1000ms);
    std::cout << "Requesting cancellation..." << std::endl;
    source.request_cancellation();
    
    // 等待任务完成
    auto result2 = task.get();
    std::cout << "Cancellable task result: " << result2 << std::endl;
    
    // 示例3: 超时任务
    std::cout << "\n--- 示例3: 超时任务 ---" << std::endl;
    auto result3 = sync_wait(timeout_task());
    std::cout << "Timeout task result: " << result3 << std::endl;
    
    // 打印统一版本报告
    std::cout << "\n--- 统一版本报告 ---" << std::endl;
    print_unified_report();
    
    return 0;
}
