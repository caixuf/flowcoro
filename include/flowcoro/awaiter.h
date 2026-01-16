#pragma once
#include <chrono>
#include <coroutine>

namespace flowcoro {

// 
class CoroutineManager;

// 
class ClockAwaiter {
private:
    std::chrono::milliseconds duration_;
    CoroutineManager* manager_;

public:
    explicit ClockAwaiter(std::chrono::milliseconds duration);
    
    // 
    template<typename Rep, typename Period>
    explicit ClockAwaiter(std::chrono::duration<Rep, Period> duration)
        : ClockAwaiter(std::chrono::duration_cast<std::chrono::milliseconds>(duration)) {}

    bool await_ready() const noexcept {
        return duration_.count() <= 0;
    }

    void await_suspend(std::coroutine_handle<> h);

    void await_resume() const noexcept {
        // 
    }
};

// SleepAwaiter
using SleepAwaiter = ClockAwaiter;
using CoroutineFriendlySleepAwaiter = ClockAwaiter;
using EnhancedSleepAwaiter = ClockAwaiter;

} // namespace flowcoro

// ClockAwaiter
#include "awaiter_impl.h"
