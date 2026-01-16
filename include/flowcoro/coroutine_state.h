#pragma once
#include <atomic>
#include <functional>
#include <array>

namespace flowcoro {

// 
enum class coroutine_state {
    created, // 
    running, // 
    suspended, // 
    completed, // 
    cancelled, // 
    destroyed, // 
    error // 
};

// 
class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};

public:
    coroutine_state_manager() = default;

    // 
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        return state_.compare_exchange_strong(from, to);
    }

    // 
    void force_transition(coroutine_state to) noexcept {
        state_.store(to, std::memory_order_release);
    }

    // 
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // 
    bool is_state(coroutine_state expected) const noexcept {
        return state_.load(std::memory_order_acquire) == expected;
    }
};

// cancellation_token
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    // vector
    static constexpr size_t MAX_CALLBACKS = 16;
    std::array<std::function<void()>, MAX_CALLBACKS> callbacks_;
    std::atomic<size_t> callback_count_{0};

public:
    cancellation_state() = default;

    //  - 
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            //  - 
            size_t count = callback_count_.load(std::memory_order_acquire);
            for (size_t i = 0; i < count; ++i) {
                if (callbacks_[i]) {
                    callbacks_[i](); // 
                }
            }
        }
    }

    // 
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

    //  - 
    template<typename Callback>
    void register_callback(Callback&& cb) {
        size_t current_count = callback_count_.load(std::memory_order_acquire);
        if (current_count >= MAX_CALLBACKS) {
            return; // 
        }

        // 
        size_t index = callback_count_.fetch_add(1, std::memory_order_acq_rel);
        if (index < MAX_CALLBACKS) {
            callbacks_[index] = std::forward<Callback>(cb);
            
            // 
            if (is_cancelled()) {
                cb(); // 
            }
        }
    }

    //  - 
    void clear_callbacks() noexcept {
        callback_count_.store(0, std::memory_order_release);
        // 
    }
};

} // namespace flowcoro
