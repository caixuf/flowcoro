#pragma once

#include "core.h"
#include "memory_pool.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <optional>

namespace flowcoro {

template<typename T>
class Channel {
private:
    struct ChannelState {
        std::queue<T> buffer;
        mutable std::mutex mutex;
        std::condition_variable cv;
        bool closed = false;
        size_t capacity;
        
        // 
        std::queue<std::coroutine_handle<>> send_waiters;
        std::queue<std::coroutine_handle<>> recv_waiters;
        
        ChannelState(size_t cap) : capacity(cap) {}
    };
    
    std::shared_ptr<ChannelState> state_;

public:
    explicit Channel(size_t capacity = 0) 
        : state_(std::allocate_shared<ChannelState>(
            flowcoro::PoolAllocator<ChannelState>{}, capacity)) {}

    // 
    Task<bool> send(T value) {
        std::unique_lock<std::mutex> lock(state_->mutex);
        
        // 
        if (state_->closed) {
            co_return false;
        }
        
        // 
        if (state_->capacity == 0 || state_->buffer.size() < state_->capacity) {
            state_->buffer.push(std::move(value));
            
            // 
            if (!state_->recv_waiters.empty()) {
                auto waiter = state_->recv_waiters.front();
                state_->recv_waiters.pop();
                lock.unlock();
                CoroutineManager::get_instance().schedule_resume(waiter);
            }
            
            co_return true;
        }
        
        // 
        struct SendAwaiter {
            ChannelState* state;
            T value;
            
            bool await_ready() { return false; }
            
            void await_suspend(std::coroutine_handle<> h) {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->closed && state->buffer.size() >= state->capacity) {
                    state->send_waiters.push(h);
                } else {
                    // 
                    CoroutineManager::get_instance().schedule_resume(h);
                }
            }
            
            bool await_resume() {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->closed) {
                    return false;
                }
                if (state->buffer.size() < state->capacity) {
                    state->buffer.push(std::move(value));
                    return true;
                }
                return false;
            }
        };
        
        lock.unlock();
        co_return co_await SendAwaiter{state_.get(), std::move(value)};
    }

    // 
    Task<std::optional<T>> recv() {
        std::unique_lock<std::mutex> lock(state_->mutex);
        
        // 
        if (!state_->buffer.empty()) {
            T value = std::move(state_->buffer.front());
            state_->buffer.pop();
            
            // 
            if (!state_->send_waiters.empty()) {
                auto waiter = state_->send_waiters.front();
                state_->send_waiters.pop();
                lock.unlock();
                CoroutineManager::get_instance().schedule_resume(waiter);
            }
            
            co_return std::optional<T>(std::move(value));
        }
        
        // 
        if (state_->closed) {
            co_return std::nullopt;
        }
        
        // 
        struct RecvAwaiter {
            ChannelState* state;
            
            bool await_ready() { return false; }
            
            void await_suspend(std::coroutine_handle<> h) {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->buffer.empty() && !state->closed) {
                    state->recv_waiters.push(h);
                } else {
                    // 
                    CoroutineManager::get_instance().schedule_resume(h);
                }
            }
            
            std::optional<T> await_resume() {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->buffer.empty()) {
                    T value = std::move(state->buffer.front());
                    state->buffer.pop();
                    return std::optional<T>(std::move(value));
                }
                return std::nullopt; // 
            }
        };
        
        lock.unlock();
        co_return co_await RecvAwaiter{state_.get()};
    }

    // 
    void close() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->closed = true;
        
        // 
        while (!state_->send_waiters.empty()) {
            auto waiter = state_->send_waiters.front();
            state_->send_waiters.pop();
            if (waiter) {
                CoroutineManager::get_instance().schedule_resume(waiter);
            }
        }
        
        while (!state_->recv_waiters.empty()) {
            auto waiter = state_->recv_waiters.front();
            state_->recv_waiters.pop();
            if (waiter) {
                CoroutineManager::get_instance().schedule_resume(waiter);
            }
        }
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->closed;
    }
};

//  - 
template<typename T>
auto make_channel(size_t capacity = 0) {
    return std::allocate_shared<Channel<T>>(
        flowcoro::PoolAllocator<Channel<T>>{}, capacity);
}

} // namespace flowcoro
