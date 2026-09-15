#pragma once

// Shared idle-wait for ThreadPool / CoroutineScheduler workers.
//
// Hot path (queue already has a runner): enqueue stays lock-free —
// wake_one() is a single atomic load when nobody is parked.
// Idle path: short pause/yield backoff, then block on futex (Linux)
// or condition_variable (elsewhere / ThreadSanitizer).
//
// This replaces uninterruptible sleep_for / naked yield loops that
// could miss a notify for up to ~10ms.

#include <atomic>
#include <climits>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(__linux__) && !defined(__SANITIZE_THREAD__)
#  if defined(__clang__) && defined(__has_feature)
#    if __has_feature(thread_sanitizer)
#      define FLOWCORO_IDLE_PARK_FUTEX 0
#    else
#      define FLOWCORO_IDLE_PARK_FUTEX 1
#    endif
#  else
#    define FLOWCORO_IDLE_PARK_FUTEX 1
#  endif
#else
#  define FLOWCORO_IDLE_PARK_FUTEX 0
#endif

#if FLOWCORO_IDLE_PARK_FUTEX
#  include <cerrno>
#  include <linux/futex.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

namespace flowcoro {

inline void cpu_pause() noexcept {
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

class IdlePark {
    static constexpr int kSpin = 64;
    static constexpr int kYield = 16;

    std::atomic<int> waiters_{0};
    std::atomic<bool> stop_{false};

#if FLOWCORO_IDLE_PARK_FUTEX
    std::atomic<int> seq_{0};

    static int futex_wait(std::atomic<int>& word, int expected) noexcept {
        while (true) {
            int rc = static_cast<int>(::syscall(
                SYS_futex, reinterpret_cast<int*>(&word), FUTEX_WAIT_PRIVATE,
                expected, nullptr, nullptr, 0));
            if (rc == 0 || errno != EINTR) {
                return rc;
            }
        }
    }
    static int futex_wake(std::atomic<int>& word, int n) noexcept {
        return static_cast<int>(::syscall(
            SYS_futex, reinterpret_cast<int*>(&word), FUTEX_WAKE_PRIVATE,
            n, nullptr, nullptr, 0));
    }
#else
    std::mutex mu_;
    std::condition_variable cv_;
#endif

public:
    void request_stop() noexcept {
        stop_.store(true, std::memory_order_release);
        wake_all();
    }

    bool stop_requested() const noexcept {
        return stop_.load(std::memory_order_acquire);
    }

    // Cheap no-op when no worker is parked.
    void wake_one() noexcept {
        if (waiters_.load(std::memory_order_acquire) == 0) {
            return;
        }
#if FLOWCORO_IDLE_PARK_FUTEX
        seq_.fetch_add(1, std::memory_order_release);
        futex_wake(seq_, 1);
#else
        cv_.notify_one();
#endif
    }

    void wake_all() noexcept {
#if FLOWCORO_IDLE_PARK_FUTEX
        seq_.fetch_add(1, std::memory_order_release);
        futex_wake(seq_, INT_MAX);
#else
        cv_.notify_all();
#endif
    }

    // Spin / yield, then block until pred() or request_stop().
    // pred() must be a wait-free observation (e.g. queue empty()), never consume work.
    template<typename Pred>
    void wait(Pred&& pred) {
        for (int i = 0; i < kSpin; ++i) {
            if (pred() || stop_.load(std::memory_order_relaxed)) {
                return;
            }
            cpu_pause();
        }
        for (int i = 0; i < kYield; ++i) {
            if (pred() || stop_.load(std::memory_order_relaxed)) {
                return;
            }
            std::this_thread::yield();
        }

        while (!pred() && !stop_.load(std::memory_order_relaxed)) {
#if FLOWCORO_IDLE_PARK_FUTEX
            const int s = seq_.load(std::memory_order_acquire);
            waiters_.fetch_add(1, std::memory_order_acq_rel);
            if (pred() || stop_.load(std::memory_order_relaxed)) {
                waiters_.fetch_sub(1, std::memory_order_release);
                return;
            }
            int rc = futex_wait(seq_, s);
            (void)rc;
            waiters_.fetch_sub(1, std::memory_order_release);
#else
            waiters_.fetch_add(1, std::memory_order_acq_rel);
            if (pred() || stop_.load(std::memory_order_relaxed)) {
                waiters_.fetch_sub(1, std::memory_order_release);
                return;
            }
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] {
                return pred() || stop_.load(std::memory_order_relaxed);
            });
            waiters_.fetch_sub(1, std::memory_order_release);
#endif
        }
    }
};

} // namespace flowcoro
