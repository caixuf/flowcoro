#pragma once
#include "awaiter.h"
#include "coroutine_manager.h"

namespace flowcoro {

// ClockAwaiter的实现
inline ClockAwaiter::ClockAwaiter(std::chrono::milliseconds duration)
    : duration_(duration), manager_(&CoroutineManager::get_instance()) {}

inline void ClockAwaiter::await_suspend(std::coroutine_handle<> h) {
    if (duration_.count() <= 0) {
        // 立即调度恢复
        manager_->schedule_resume(h);
        return;
    }

    // 添加到定时器队列
    auto when = std::chrono::steady_clock::now() + duration_;
    manager_->add_timer(when, h);
}

} // namespace flowcoro
