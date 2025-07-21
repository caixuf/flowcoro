#pragma once

#include <coroutine>
#include <atomic>
#include <memory>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <vector>
#include <utility>  // 为了std::exchange

// 前向声明，避免循环依赖
namespace flowcoro {
    template<typename T> class Task;
    class GlobalThreadPool;
}

namespace flowcoro::lifecycle {

// 协程生命周期状态枚举
enum class coroutine_state {
    created,    // 刚创建，尚未开始执行
    running,    // 正在运行
    suspended,  // 已挂起等待
    completed,  // 正常完成
    destroyed,  // 已销毁（发生异常）
    cancelled   // 被取消
};

// 获取状态名称的辅助函数
inline const char* state_name(coroutine_state state) noexcept {
    switch (state) {
        case coroutine_state::created: return "created";
        case coroutine_state::running: return "running";
        case coroutine_state::suspended: return "suspended";
        case coroutine_state::completed: return "completed";
        case coroutine_state::destroyed: return "destroyed";
        case coroutine_state::cancelled: return "cancelled";
        default: return "unknown";
    }
}

// 安全的协程句柄包装器
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> destroyed_{false};
    mutable std::mutex debug_mutex_; // 用于调试信息的同步
    
public:
    safe_coroutine_handle() = default;
    
    explicit safe_coroutine_handle(std::coroutine_handle<> h) noexcept 
        : handle_(h) {}
    
    ~safe_coroutine_handle() {
        destroy();
    }
    
    // 移动构造和赋值
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept 
        : handle_(std::exchange(other.handle_, {}))
        , destroyed_(other.destroyed_.exchange(false)) {}
    
    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
            destroyed_.store(other.destroyed_.exchange(false));
        }
        return *this;
    }
    
    // 禁止拷贝
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    // 检查句柄是否有效
    bool valid() const noexcept {
        return handle_ && !destroyed_.load(std::memory_order_acquire);
    }
    
    // 安全恢复协程
    void resume() {
        if (valid() && !handle_.done()) {
            try {
                handle_.resume();
            } catch (...) {
                // 恢复过程中的异常会被promise处理
                // 这里不重新抛出，避免双重异常
            }
        }
    }
    
    // 检查是否完成
    bool done() const noexcept {
        return !handle_ || destroyed_.load(std::memory_order_acquire) || handle_.done();
    }
    
    // 安全销毁
    void destroy() noexcept {
        bool expected = false;
        if (destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            if (handle_) {
                try {
                    handle_.destroy();
                } catch (...) {
                    // 忽略析构函数中的异常，但可以记录日志
                    // 在生产环境中，这里应该记录到日志系统
                }
                handle_ = {};
            }
        }
    }
    
    // 获取句柄地址（用于调试）
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
    
    // 获取promise（类型安全）
    template<typename Promise>
    Promise& promise() const {
        if (!handle_) {
            throw std::runtime_error("Attempting to access promise of invalid coroutine handle");
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }
    
    // 调试信息
    bool is_destroyed() const noexcept {
        return destroyed_.load(std::memory_order_acquire);
    }
};

// 协程状态管理器
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};
    std::chrono::steady_clock::time_point creation_time_;
    std::atomic<std::chrono::steady_clock::time_point*> completion_time_{nullptr};
    
public:
    coroutine_state_manager() 
        : creation_time_(std::chrono::steady_clock::now()) {}
    
    ~coroutine_state_manager() {
        // 清理完成时间指针
        auto* time_ptr = completion_time_.load();
        delete time_ptr;
    }
    
    // 获取当前状态
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    
    // 尝试状态转换
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        auto result = state_.compare_exchange_strong(from, to, std::memory_order_acq_rel);
        if (result && is_terminal_state(to)) {
            mark_completion();
        }
        return result;
    }
    
    // 强制状态转换（用于异常情况）
    void force_transition(coroutine_state to) noexcept {
        if (is_terminal_state(to)) {
            mark_completion();
        }
        state_.store(to, std::memory_order_release);
    }
    
    // 获取生命周期时长
    std::chrono::milliseconds get_lifetime() const {
        auto end = get_completion_time_or_now();
        auto duration = end - creation_time_;
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    }
    
    // 检查是否处于活跃状态
    bool is_active() const noexcept {
        auto s = get_state();
        return s == coroutine_state::running || s == coroutine_state::suspended;
    }
    
    // 检查是否已完成（包括正常完成和异常）
    bool is_terminal() const noexcept {
        return is_terminal_state(get_state());
    }
    
    // 获取创建时间
    std::chrono::steady_clock::time_point get_creation_time() const noexcept {
        return creation_time_;
    }

private:
    // 标记完成时间
    void mark_completion() noexcept {
        auto* expected = completion_time_.load();
        if (expected == nullptr) {
            auto* new_time = new(std::nothrow) std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::now()
            );
            if (new_time && !completion_time_.compare_exchange_strong(expected, new_time)) {
                delete new_time;  // 另一个线程已经设置了完成时间
            }
        }
    }
    
    // 获取完成时间或当前时间
    std::chrono::steady_clock::time_point get_completion_time_or_now() const {
        auto* time_ptr = completion_time_.load();
        return time_ptr ? *time_ptr : std::chrono::steady_clock::now();
    }
    
    // 检查是否为终止状态
    static bool is_terminal_state(coroutine_state state) noexcept {
        return state >= coroutine_state::completed;
    }
};

// 取消状态管理
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    mutable std::mutex callbacks_mutex_;
    std::vector<std::function<void()>> callbacks_;
    
public:
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    // 请求取消
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // 首次取消，执行所有回调
            execute_callbacks();
        }
    }
    
    // 注册取消回调
    template<typename Callback>
    void register_callback(Callback&& cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        if (is_cancelled()) {
            // 已经取消，立即执行回调
            try {
                cb();
            } catch (...) {
                // 忽略回调异常
            }
        } else {
            callbacks_.emplace_back(std::forward<Callback>(cb));
        }
    }
    
    // 清除所有回调（通常在析构时调用）
    void clear_callbacks() noexcept {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.clear();
    }

private:
    // 执行所有回调
    void execute_callbacks() noexcept {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (auto& callback : callbacks_) {
            try {
                callback();
            } catch (...) {
                // 忽略回调异常，但在生产环境中应该记录日志
            }
        }
        callbacks_.clear();
    }
};

} // namespace flowcoro::lifecycle
