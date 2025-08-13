#pragma once
#include <atomic>
#include <functional>
#include <array>

namespace flowcoro {

// 协程状态枚举
enum class coroutine_state {
    created, // 刚创建，尚未开始执行
    running, // 正在运行
    suspended, // 挂起等待
    completed, // 已完成
    cancelled, // 已取消
    destroyed, // 已销毁
    error // 执行出错
};

// 协程状态管理器
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};

public:
    coroutine_state_manager() = default;

    // 尝试状态转换
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        return state_.compare_exchange_strong(from, to);
    }

    // 强制状态转换
    void force_transition(coroutine_state to) noexcept {
        state_.store(to, std::memory_order_release);
    }

    // 获取当前状态
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // 检查是否处于某个状态
    bool is_state(coroutine_state expected) const noexcept {
        return state_.load(std::memory_order_acquire) == expected;
    }
};

// 无锁取消状态（被cancellation_token使用）
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    // 使用无锁数组替代vector，避免互斥锁
    static constexpr size_t MAX_CALLBACKS = 16;
    std::array<std::function<void()>, MAX_CALLBACKS> callbacks_;
    std::atomic<size_t> callback_count_{0};

public:
    cancellation_state() = default;

    // 请求取消 - 无锁实现
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            // 第一次取消，触发回调 - 无锁遍历
            size_t count = callback_count_.load(std::memory_order_acquire);
            for (size_t i = 0; i < count; ++i) {
                if (callbacks_[i]) {
                    callbacks_[i](); // 直接调用，不捕获异常
                }
            }
        }
    }

    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

    // 注册取消回调 - 无锁实现
    template<typename Callback>
    void register_callback(Callback&& cb) {
        size_t current_count = callback_count_.load(std::memory_order_acquire);
        if (current_count >= MAX_CALLBACKS) {
            return; // 超出容量限制，忽略新回调
        }

        // 原子性地添加回调
        size_t index = callback_count_.fetch_add(1, std::memory_order_acq_rel);
        if (index < MAX_CALLBACKS) {
            callbacks_[index] = std::forward<Callback>(cb);
            
            // 如果已经取消，立即调用回调
            if (is_cancelled()) {
                cb(); // 直接调用，不捕获异常
            }
        }
    }

    // 清空回调 - 无锁实现
    void clear_callbacks() noexcept {
        callback_count_.store(0, std::memory_order_release);
        // 不需要清空数组内容，只需重置计数器
    }
};

} // namespace flowcoro
