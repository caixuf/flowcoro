#pragma once
#include <chrono>
#include <coroutine>

namespace flowcoro {

// 前向声明
class CoroutineManager;

// 安全的时钟等待器
class ClockAwaiter {
private:
    std::chrono::milliseconds duration_;
    CoroutineManager* manager_;

public:
    explicit ClockAwaiter(std::chrono::milliseconds duration);
    
    // 模板构造函数，支持任意时间类型
    template<typename Rep, typename Period>
    explicit ClockAwaiter(std::chrono::duration<Rep, Period> duration)
        : ClockAwaiter(std::chrono::duration_cast<std::chrono::milliseconds>(duration)) {}

    bool await_ready() const noexcept {
        return duration_.count() <= 0;
    }

    void await_suspend(std::coroutine_handle<> h);

    void await_resume() const noexcept {
        // 定时器完成
    }
};

// 替换原有的SleepAwaiter
using SleepAwaiter = ClockAwaiter;
using CoroutineFriendlySleepAwaiter = ClockAwaiter;
using EnhancedSleepAwaiter = ClockAwaiter;

} // namespace flowcoro

// 包含ClockAwaiter的实现
#include "awaiter_impl.h"
