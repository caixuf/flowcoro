#pragma once

#include "core.h"
#include <atomic>
#include <queue>
#include <mutex>

namespace flowcoro::sync {

// 协程友好的互斥锁
class AsyncMutex {
private:
    std::atomic<bool> locked_{false};
    std::queue<std::coroutine_handle<>> waiters_;
    std::mutex waiters_mutex_;

public:
    struct LockAwaiter {
        AsyncMutex* mutex;
        
        bool await_ready() {
            return !mutex->locked_.exchange(true, std::memory_order_acquire);
        }
        
        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lock(mutex->waiters_mutex_);
            mutex->waiters_.push(h);
        }
        
        void await_resume() {}
    };

    LockAwaiter lock() {
        return LockAwaiter{this};
    }

    void unlock() {
        std::lock_guard<std::mutex> lock(waiters_mutex_);
        locked_.store(false, std::memory_order_release);
        
        if (!waiters_.empty()) {
            auto waiter = waiters_.front();
            waiters_.pop();
            CoroutineManager::get_instance().schedule_resume(waiter);
        }
    }
};

// 协程友好的信号量 - 基于cppcoro设计
class AsyncSemaphore {
private:
    std::atomic<std::int32_t> count_;
    mutable std::mutex waiters_mutex_;
    std::queue<std::coroutine_handle<>> waiters_;

public:
    explicit AsyncSemaphore(std::int32_t initial_count) 
        : count_(initial_count) {}

    struct AcquireAwaiter {
        AsyncSemaphore* semaphore;
        
        bool await_ready() noexcept {
            return semaphore->try_acquire();
        }
        
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            std::lock_guard<std::mutex> lock(semaphore->waiters_mutex_);
            
            // 再次尝试获取（持锁状态下）
            if (semaphore->try_acquire()) {
                return false; // 成功获取，不需要挂起
            }
            
            // 无法获取，加入等待队列
            semaphore->waiters_.push(h);
            return true; // 需要挂起
        }
        
        void await_resume() noexcept {}
    };

    AcquireAwaiter acquire() noexcept {
        return AcquireAwaiter{this};
    }

    void release() noexcept {
        std::lock_guard<std::mutex> lock(waiters_mutex_);
        
        // 如果有等待者，直接唤醒一个
        if (!waiters_.empty()) {
            auto waiter = waiters_.front();
            waiters_.pop();
            CoroutineManager::get_instance().schedule_resume(waiter);
        } else {
            // 没有等待者，增加计数
            count_.fetch_add(1, std::memory_order_release);
        }
    }

    std::int32_t available() const noexcept {
        return std::max(std::int32_t(0), count_.load(std::memory_order_relaxed));
    }

private:
    bool try_acquire() noexcept {
        std::int32_t expected = count_.load(std::memory_order_relaxed);
        while (expected > 0) {
            if (count_.compare_exchange_weak(expected, expected - 1, 
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }
};

// 协程友好的条件变量
class AsyncConditionVariable {
private:
    std::queue<std::coroutine_handle<>> waiters_;
    std::mutex waiters_mutex_;

public:
    struct WaitAwaiter {
        AsyncConditionVariable* cv;
        
        bool await_ready() { return false; }
        
        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lock(cv->waiters_mutex_);
            cv->waiters_.push(h);
        }
        
        void await_resume() {}
    };

    WaitAwaiter wait() {
        return WaitAwaiter{this};
    }

    void notify_one() {
        std::lock_guard<std::mutex> lock(waiters_mutex_);
        if (!waiters_.empty()) {
            auto waiter = waiters_.front();
            waiters_.pop();
            CoroutineManager::get_instance().schedule_resume(waiter);
        }
    }

    void notify_all() {
        std::lock_guard<std::mutex> lock(waiters_mutex_);
        while (!waiters_.empty()) {
            auto waiter = waiters_.front();
            waiters_.pop();
            CoroutineManager::get_instance().schedule_resume(waiter);
        }
    }
};

} // namespace flowcoro::sync
