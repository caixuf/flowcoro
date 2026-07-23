#pragma once

// =====================================================================
// flowcoro/timeout.h
//
// 可组合超时 awaitable: with_timeout(task, duration)
//
// 解决问题：原 when_any_timeout 把整个超时塞进 when_any 的轮询循环里，
// 现在基于事件驱动的 when_any 实现，超时和原任务公平竞争。
//
// 用法：
//   Task<int> t = some_async_work();
//   int v = co_await with_timeout(std::move(t), 100ms);
//   // 超时则抛 TimeoutException
//
// 设计：复用 when_any + ClockAwaiter。胜者索引 0 = 原任务完成，
// 索引 1 = 超时。若超时胜出则抛 TimeoutException。
// =====================================================================

#include <any>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>
#include "task.h"
#include "awaiter.h"
#include "when_any.h"

namespace flowcoro {

// 超时异常
class TimeoutException : public std::runtime_error {
public:
    TimeoutException()
        : std::runtime_error("operation timed out") {}
    explicit TimeoutException(const char* msg)
        : std::runtime_error(msg) {}
};

namespace detail {

// 超时辅助任务：仅用于和原任务做 race
template<typename Rep, typename Period>
Task<void> make_timeout_task(std::chrono::duration<Rep, Period> timeout) {
    co_await ClockAwaiter(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
    co_return;
}

} // namespace detail

// =====================================================================
// with_timeout: 对一个 Task<T> 应用超时
// 返回 Task<T>，若超时则抛 TimeoutException
// =====================================================================
template<typename T, typename Rep, typename Period>
Task<T> with_timeout(Task<T> task, std::chrono::duration<Rep, Period> timeout) {
    auto timeout_task = detail::make_timeout_task(timeout);
    // 用 when_any 让两者 race
    auto result = co_await when_any(std::move(task), std::move(timeout_task));

    auto winner_idx = result.first;
    auto& any_val = result.second;

    if (winner_idx == 0) {
        // 原任务胜出
        if (!any_val.has_value()) {
            // void 路径不应走到这里（T 非 void）
            throw TimeoutException("with_timeout: task returned no value");
        }
        // 检查是否捕获了异常
        auto* exc_ptr = std::any_cast<std::exception_ptr>(&any_val);
        if (exc_ptr && *exc_ptr) {
            std::rethrow_exception(*exc_ptr);
        }
        // 取出真实值
        auto* val_ptr = std::any_cast<T>(&any_val);
        if (!val_ptr) {
            throw TimeoutException("with_timeout: result type mismatch");
        }
        co_return std::move(*val_ptr);
    } else {
        // 超时胜出
        throw TimeoutException();
    }
}

// void 特化
template<typename Rep, typename Period>
Task<void> with_timeout(Task<void> task, std::chrono::duration<Rep, Period> timeout) {
    auto timeout_task = detail::make_timeout_task(timeout);

    auto result = co_await when_any(std::move(task), std::move(timeout_task));

    auto winner_idx = result.first;
    auto& any_val = result.second;

    if (winner_idx == 0) {
        // 原任务胜出
        if (any_val.has_value()) {
            // 异常？检查
            auto* exc_ptr = std::any_cast<std::exception_ptr>(&any_val);
            if (exc_ptr && *exc_ptr) {
                std::rethrow_exception(*exc_ptr);
            }
        }
        // void：直接返回
        co_return;
    } else {
        throw TimeoutException();
    }
}

// =====================================================================
// 装饰器风格的 awaiter: timeout_awaitable(awaitable, duration)
// 用于对任意 awaitable（不必是 Task）应用超时
// 当前实现：包装成 Task<void> + 1ms granularity timer，简化版
// 完整实现需要把 awaitable 的 await_suspend 改造为可被定时器取消，
// 这里只对 Task 提供基础能力
// =====================================================================

} // namespace flowcoro
