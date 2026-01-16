#pragma once
#include <coroutine>
#include <atomic>
#include <utility>
#include "logger.h"

namespace flowcoro {

// 
class CoroutineManager;

// 
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> valid_{false};

public:
    safe_coroutine_handle() = default;

    // 
    template<typename Promise>
    explicit safe_coroutine_handle(std::coroutine_handle<Promise> h)
        : handle_(h), valid_(h && !h.done()) {}

    // void
    explicit safe_coroutine_handle(std::coroutine_handle<> h)
        : handle_(h), valid_(h && !h.done()) {}

    // 
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

    // 
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;

    ~safe_coroutine_handle() {
        if (valid() && handle_) {
            if (handle_) {
                handle_.destroy(); // 
            }
        }
    }

    // 
    bool valid() const noexcept {
        return valid_.load(std::memory_order_acquire) && handle_;
    }

    // 
    bool done() const noexcept {
        return !valid() || handle_.done();
    }

    // 
    void resume();  // safe_handle_impl.h

    // Promise
    template<typename Promise>
    Promise& promise() {
        if (!valid()) {
            LOG_ERROR("Invalid coroutine handle in promise()");
            static Promise default_promise{};
            return default_promise;
        }
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }

    // 
    void invalidate() noexcept {
        valid_.store(false, std::memory_order_release);
    }

    // 
    void* address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
};

} // namespace flowcoro

// 
#include "safe_handle_impl.h"
