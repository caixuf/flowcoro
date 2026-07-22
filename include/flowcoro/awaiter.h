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
    // 注册的 timer_id（0 表示无定时器 / 已完成 / 未注册）
    // 用于在协程被销毁时取消未触发的定时器，避免 use-after-free。
    uint64_t timer_id_{0};
    // 标记是否已被 await_suspend 注册过，避免重复取消
    bool registered_{false};

public:
    explicit ClockAwaiter(std::chrono::milliseconds duration);
    
    // 模板构造函数，支持任意时间类型
    template<typename Rep, typename Period>
    explicit ClockAwaiter(std::chrono::duration<Rep, Period> duration)
        : ClockAwaiter(std::chrono::duration_cast<std::chrono::milliseconds>(duration)) {}

    // 析构时取消未触发的定时器，防止定时器恢复已销毁的协程
    ~ClockAwaiter();

    ClockAwaiter(const ClockAwaiter&) = delete;
    ClockAwaiter& operator=(const ClockAwaiter&) = delete;
    ClockAwaiter(ClockAwaiter&& other) noexcept
        : duration_(other.duration_), manager_(other.manager_),
          timer_id_(other.timer_id_), registered_(other.registered_) {
        other.timer_id_ = 0;
        other.registered_ = false;
    }
    ClockAwaiter& operator=(ClockAwaiter&& other) noexcept {
        if (this != &other) {
            // 先取消自己的定时器
            if (manager_ && registered_ && timer_id_ != 0) {
                manager_->cancel_timer(timer_id_);
            }
            duration_ = other.duration_;
            manager_ = other.manager_;
            timer_id_ = other.timer_id_;
            registered_ = other.registered_;
            other.timer_id_ = 0;
            other.registered_ = false;
        }
        return *this;
    }

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
