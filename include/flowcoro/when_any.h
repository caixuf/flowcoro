#pragma once
#include <tuple>
#include <any>
#include <utility>
#include <type_traits>
#include "task.h"
#include "awaiter.h"

namespace flowcoro {

// when_all - task
template<typename... Tasks>
Task<std::tuple<decltype(std::declval<Tasks>().get())...>> when_all(Tasks&&... tasks) {
    // fold expression
    auto execute_all = []<typename... Ts>(Ts&&... ts) -> Task<std::tuple<decltype(ts.get())...>> {
        // tuple
        std::tuple<decltype(ts.get())...> results;

        // task
        auto await_task = [&]<std::size_t... Is>(std::index_sequence<Is...>) -> Task<void> {
            // fold expressiontask
            ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
            co_return;
        };

        // task
        co_await await_task(std::index_sequence_for<Ts...>{});

        // 
        co_return std::move(results);
    };

    // 
    co_return co_await execute_all(std::forward<Tasks>(tasks)...);
}

// 
template<std::size_t Index, typename Task>
bool check_single_task(Task& task, bool& found, std::size_t& winner_index, std::any& result) {
    if (found) return false; // 
    
    if (task.await_ready()) {
        // 
        try {
            auto task_result = task.await_resume();
            result = std::make_any<std::decay_t<decltype(task_result)>>(std::move(task_result));
            winner_index = Index;
            found = true;
            return true;
        } catch (...) {
            // 
            winner_index = Index;
            found = true;
            // result
            return true;
        }
    }
    return false;
}

// when_any - task
template<typename... Tasks>
Task<std::pair<std::size_t, std::any>> when_any(Tasks&&... tasks) {
    // fold expressionwhen_all
    auto execute_any = []<typename... Ts>(Ts&&... ts) -> Task<std::pair<std::size_t, std::any>> {
        // taskstuple
        std::tuple<Ts...> task_tuple(std::forward<Ts>(ts)...);
        
        // 
        while (true) {
            // 
            std::size_t winner_index = std::size_t(-1);
            std::any result;
            bool found = false;
            
            // task
            auto check_tasks = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                // fold expression
                (check_single_task<Is>(std::get<Is>(task_tuple), found, winner_index, result) || ...);
            };
            
            check_tasks(std::index_sequence_for<Ts...>{});
            
            if (found) {
                co_return std::make_pair(winner_index, std::move(result));
            }
            
            //  - ClockAwaiter
            co_await ClockAwaiter(std::chrono::milliseconds(1));
        }
    };

    // 
    co_return co_await execute_any(std::forward<Tasks>(tasks)...);
}

// when_any - 
template<typename TaskType, typename Duration>
auto when_any_timeout(TaskType&& task, Duration timeout) {
    auto timeout_task = [timeout]() -> Task<bool> {
        co_await ClockAwaiter(timeout);
        co_return false; // false
    }();
    
    return when_any(std::forward<TaskType>(task), std::move(timeout_task));
}

} // namespace flowcoro
