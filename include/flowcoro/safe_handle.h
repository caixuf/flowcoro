#pragma once
#include <coroutine>
#include <atomic>
#include <utility>
#include "logger.h"

namespace flowcoro {

// 前向声明
class CoroutineManager;

// 安全协程句柄包装器
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> valid_{false};

public:
    safe_coroutine_handle() = default;

    // 从任意类型的协程句柄构造
    template<typename Promise>
    explicit safe_coroutine_handle(std::coroutine_handle<Promise> h)
        : handle_(h), valid_(h && !h.done()) {}

    // 从void句柄构造（特化版本）
    explicit safe_coroutine_handle(std::coroutine_handle<> h)
        : handle_(h), valid_(h && !h.done()) {}

    // 移动构造
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept
        : handle_(std::exchange(other.handle_, {}))
        , valid_(other.valid_.exchange(false)) {}

    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept {
        if (this != &other) {
            handle_ = std::exchange(other.handle_, {});
            valid_.store(other.valid_.exchange(false));
        }
        return *this;
    }

    // 禁止拷贝
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;

    ~safe_coroutine_handle() {
        if (valid() && handle_) {
            if (handle_) {
                handle_.destroy(); // 直接销毁，不捕获异常
            }
        }
    }

    // 检查句柄是否有效
    bool valid() const noexcept {
        return valid_.load(std::memory_order_acquire) && handle_;
    }

    // 检查协程是否完成
    bool done() const noexcept {
        return !valid() || handle_.done();
    }

    // 恢复协程执行
    void resume();  // 实现在safe_handle_impl.h中

    // 获取Promise（类型安全）
    template<typename Promise>
    Promise& promise() {
        if (!valid()) {
            LOG_ERROR("Invalid coroutine handle in promise()");
            static Promise default_promise{};
            return default_promise;
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }

    // 使句柄无效
    void invalidate() noexcept {
        valid_.store(false, std::memory_order_release);
    }

    // 获取原始地址
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
};

} // namespace flowcoro

// 包含实现
#include "safe_handle_impl.h"
