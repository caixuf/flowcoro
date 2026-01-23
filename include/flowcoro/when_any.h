#pragma once
#include <tuple>
#include <any>
#include <utility>
#include <type_traits>
#include "task.h"
#include "awaiter.h"

namespace flowcoro {

// when_all实现 - 等待所有task完成
template<typename... Tasks>
Task<std::tuple<decltype(std::declval<Tasks>().get())...>> when_all(Tasks&&... tasks) {
    // 使用fold expression和参数展开
    auto execute_all = []<typename... Ts>(Ts&&... ts) -> Task<std::tuple<decltype(ts.get())...>> {
        // 创建一个tuple来存储所有结果
        std::tuple<decltype(ts.get())...> results;

        // 使用索引展开来逐个等待每个task
        auto await_task = [&]<std::size_t... Is>(std::index_sequence<Is...>) -> Task<void> {
            // 使用fold expression按顺序等待所有task
            ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
            co_return;
        };

        // 等待所有task完成
        co_await await_task(std::index_sequence_for<Ts...>{});

        // 返回所有结果
        co_return std::move(results);
    };

    // 调用执行函数
    co_return co_await execute_all(std::forward<Tasks>(tasks)...);
}

// 检查单个任务的辅助函数模板
template<std::size_t Index, typename Task>
bool check_single_task(Task& task, bool& found, std::size_t& winner_index, std::any& result) {
    if (found) return false; // 已找到完成的任务
    
    if (task.await_ready()) {
        // 任务已完成，获取结果
        try {
            auto task_result = task.await_resume();
            result = std::make_any<std::decay_t<decltype(task_result)>>(std::move(task_result));
            winner_index = Index;
            found = true;
            return true;
        } catch (...) {
            // 如果有异常，也算完成
            winner_index = Index;
            found = true;
            // 可以在这里设置异常信息到result中
            return true;
        }
    }
    return false;
}

// when_any实现 - 等待任意一个task完成
template<typename... Tasks>
Task<std::pair<std::size_t, std::any>> when_any(Tasks&&... tasks) {
    // 使用fold expression和参数展开，参考when_all的实现方式
    auto execute_any = []<typename... Ts>(Ts&&... ts) -> Task<std::pair<std::size_t, std::any>> {
        // 将所有tasks存储在tuple中，避免引用问题
        std::tuple<Ts...> task_tuple(std::forward<Ts>(ts)...);
        
        // 启动所有任务的轮询检查
        while (true) {
            // 检查每个任务是否完成
            std::size_t winner_index = std::size_t(-1);
            std::any result;
            bool found = false;
            
            // 使用索引展开来检查每个task
            auto check_tasks = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                // 使用fold expression检查所有任务
                (check_single_task<Is>(std::get<Is>(task_tuple), found, winner_index, result) || ...);
            };
            
            check_tasks(std::index_sequence_for<Ts...>{});
            
            if (found) {
                co_return std::make_pair(winner_index, std::move(result));
            }
            
            // 短暂休眠避免忙等待
            // 注意：这是一个简化实现，使用轮询机制
            // TODO: 考虑使用事件驱动或条件变量以提高效率和降低CPU使用
            co_await ClockAwaiter(std::chrono::milliseconds(1));
        }
    };

    // 调用执行函数
    co_return co_await execute_any(std::forward<Tasks>(tasks)...);
}

// 便捷的when_any重载 - 支持超时
template<typename TaskType, typename Duration>
auto when_any_timeout(TaskType&& task, Duration timeout) {
    auto timeout_task = [timeout]() -> Task<bool> {
        co_await ClockAwaiter(timeout);
        co_return false; // 超时返回false
    }();
    
    return when_any(std::forward<TaskType>(task), std::move(timeout_task));
}

} // namespace flowcoro
