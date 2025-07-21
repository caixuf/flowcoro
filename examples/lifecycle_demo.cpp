#include "enhanced_coroutine.h"
#include "../core.h"
#include <iostream>
#include <thread>
#include <vector>

using namespace flowcoro;

// 示例1: 基本的协程生命周期管理
enhanced_task<int> basic_lifecycle_demo() {
    std::cout << "协程开始执行\n";
    
    // 模拟一些异步工作
    co_await sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "协程即将完成\n";
    co_return 42;
}

// 示例2: 带取消支持的协程
enhanced_task<std::string> cancellable_demo(cancellation_token token) {
    for (int i = 0; i < 10; ++i) {
        // 检查取消状态
        token.throw_if_cancelled();
        
        std::cout << "处理步骤: " << i << "\n";
        co_await sleep_for(std::chrono::milliseconds(50));
    }
    
    co_return "处理完成";
}

// 示例3: 超时处理的协程
enhanced_task<int> timeout_demo() {
    std::cout << "开始长时间运行的任务\n";
    
    // 模拟长时间运行的任务
    co_await sleep_for(std::chrono::milliseconds(2000));
    
    co_return 100;
}

// 示例4: 协程异常处理
enhanced_task<void> exception_demo() {
    std::cout << "即将抛出异常\n";
    co_await sleep_for(std::chrono::milliseconds(10));
    
    throw std::runtime_error("协程中的异常");
}

// 示例5: 多协程并发管理
enhanced_task<std::vector<int>> concurrent_demo() {
    std::vector<enhanced_task<int>> tasks;
    
    // 创建多个并发任务
    for (int i = 0; i < 5; ++i) {
        tasks.push_back([i]() -> enhanced_task<int> {
            co_await sleep_for(std::chrono::milliseconds(100 * (i + 1)));
            co_return i * 10;
        }());
    }
    
    std::vector<int> results;
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
    
    co_return results;
}

void demo_basic_lifecycle() {
    std::cout << "\n=== 基本生命周期管理演示 ===\n";
    
    auto task = basic_lifecycle_demo();
    
    std::cout << "任务状态: " << static_cast<int>(task.get_state()) << "\n";
    std::cout << "是否就绪: " << (task.is_ready() ? "是" : "否") << "\n";
    
    try {
        int result = task.get();
        std::cout << "任务结果: " << result << "\n";
        std::cout << "任务生命周期: " << task.get_lifetime().count() << "ms\n";
    } catch (const std::exception& e) {
        std::cout << "任务异常: " << e.what() << "\n";
    }
    
    std::cout << "最终状态: " << static_cast<int>(task.get_state()) << "\n";
}

void demo_cancellation() {
    std::cout << "\n=== 取消机制演示 ===\n";
    
    cancellation_source cancel_source;
    auto token = cancel_source.get_token();
    
    auto task = cancellable_demo(token);
    
    // 在另一个线程中取消任务
    std::thread cancel_thread([&cancel_source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "发送取消信号\n";
        cancel_source.cancel();
    });
    
    try {
        auto result = task.get();
        std::cout << "任务结果: " << result << "\n";
    } catch (const operation_cancelled_exception& e) {
        std::cout << "任务被取消: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cout << "任务异常: " << e.what() << "\n";
    }
    
    cancel_thread.join();
    
    std::cout << "任务是否被取消: " << (task.is_cancelled() ? "是" : "否") << "\n";
}

void demo_timeout() {
    std::cout << "\n=== 超时处理演示 ===\n";
    
    auto task = timeout_demo();
    
    // 设置超时时间
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout_duration = std::chrono::milliseconds(500);
    
    bool timed_out = false;
    std::thread timeout_thread([&task, &timed_out, timeout_duration]() {
        std::this_thread::sleep_for(timeout_duration);
        if (!task.is_ready()) {
            std::cout << "任务超时，发送取消信号\n";
            task.cancel();
            timed_out = true;
        }
    });
    
    try {
        int result = task.get();
        std::cout << "任务结果: " << result << "\n";
    } catch (const operation_cancelled_exception& e) {
        if (timed_out) {
            std::cout << "任务超时被取消\n";
        } else {
            std::cout << "任务被取消: " << e.what() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "任务异常: " << e.what() << "\n";
    }
    
    timeout_thread.join();
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    std::cout << "实际耗时: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() 
              << "ms\n";
}

void demo_exception_handling() {
    std::cout << "\n=== 异常处理演示 ===\n";
    
    auto task = exception_demo();
    
    std::cout << "初始状态: " << static_cast<int>(task.get_state()) << "\n";
    
    try {
        task.get();
        std::cout << "任务正常完成\n";
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << "\n";
    }
    
    std::cout << "最终状态: " << static_cast<int>(task.get_state()) << "\n";
    std::cout << "任务生命周期: " << task.get_lifetime().count() << "ms\n";
}

void demo_concurrent_management() {
    std::cout << "\n=== 并发管理演示 ===\n";
    
    auto task = concurrent_demo();
    
    try {
        auto results = task.get();
        std::cout << "并发任务结果: ";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << results[i];
            if (i < results.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
        
        std::cout << "总耗时: " << task.get_lifetime().count() << "ms\n";
    } catch (const std::exception& e) {
        std::cout << "并发任务异常: " << e.what() << "\n";
    }
}

void demo_state_tracking() {
    std::cout << "\n=== 状态追踪演示 ===\n";
    
    auto task = basic_lifecycle_demo();
    
    // 状态名称映射
    auto state_name = [](detail::coroutine_state state) -> const char* {
        switch (state) {
            case detail::coroutine_state::created: return "已创建";
            case detail::coroutine_state::running: return "运行中";
            case detail::coroutine_state::suspended: return "已挂起";
            case detail::coroutine_state::completed: return "已完成";
            case detail::coroutine_state::destroyed: return "已销毁";
            case detail::coroutine_state::cancelled: return "已取消";
            default: return "未知";
        }
    };
    
    std::cout << "初始状态: " << state_name(task.get_state()) << "\n";
    
    // 监控状态变化
    std::thread monitor_thread([&task, &state_name]() {
        auto last_state = task.get_state();
        
        while (!task.is_ready() && task.get_state() != detail::coroutine_state::cancelled) {
            auto current_state = task.get_state();
            if (current_state != last_state) {
                std::cout << "状态变化: " << state_name(last_state) 
                          << " -> " << state_name(current_state) << "\n";
                last_state = current_state;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    try {
        int result = task.get();
        std::cout << "任务结果: " << result << "\n";
    } catch (const std::exception& e) {
        std::cout << "任务异常: " << e.what() << "\n";
    }
    
    monitor_thread.join();
    
    std::cout << "最终状态: " << state_name(task.get_state()) << "\n";
    std::cout << "生命周期: " << task.get_lifetime().count() << "ms\n";
    std::cout << "是否活跃: " << (task.get_state() == detail::coroutine_state::running ||
                                  task.get_state() == detail::coroutine_state::suspended ? "是" : "否") << "\n";
}

int main() {
    std::cout << "FlowCoro 协程生命周期管理演示\n";
    std::cout << "=====================================\n";
    
    try {
        // 运行各种演示
        demo_basic_lifecycle();
        demo_cancellation();
        demo_timeout();
        demo_exception_handling();
        demo_concurrent_management();
        demo_state_tracking();
        
    } catch (const std::exception& e) {
        std::cerr << "演示程序异常: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\n所有演示完成！\n";
    return 0;
}
