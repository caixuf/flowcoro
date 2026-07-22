#pragma once
#include "awaiter.h"
#include "coroutine_manager.h"

namespace flowcoro {

// ClockAwaiter的实现
inline ClockAwaiter::ClockAwaiter(std::chrono::milliseconds duration)
    : duration_(duration), manager_(&CoroutineManager::get_instance()) {}

inline ClockAwaiter::~ClockAwaiter() {
    // 若定时器已注册但尚未触发（await_resume 未被调用），取消之。
    // 这防止协程被销毁后定时器仍尝试恢复其 handle（use-after-free）。
    if (manager_ && registered_ && timer_id_ != 0) {
        manager_->cancel_timer(timer_id_);
    }
}

inline void ClockAwaiter::await_suspend(std::coroutine_handle<> h) {
    if (duration_.count() <= 0) {
        // 立即调度恢复
        manager_->schedule_resume(h);
        return;
    }

    // 添加到定时器队列，并记录 timer_id 以便析构时取消
    auto when = std::chrono::steady_clock::now() + duration_;
    timer_id_ = manager_->add_timer_enhanced(when, h);
    registered_ = true;
}

} // namespace flowcoro
