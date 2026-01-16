#pragma once
#include <coroutine>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <exception>
#include "coroutine_manager.h"
#include "logger.h"

namespace flowcoro {

// Promise
template<typename T>
class AsyncPromise {
public:
    using Callback = std::function<void(const T&)>;

    AsyncPromise() : state_(std::make_shared<State>()) {}

    void set_value(const T& value) {
        LOG_DEBUG("Setting AsyncPromise value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->value_ = value;
            state_->ready_.store(true, std::memory_order_release);

            // 
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        // 
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise");
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);

            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise exception");
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> h) {
        // 
        std::lock_guard<std::mutex> lock(state_->mutex_);

        // 
        if (state_->ready_.load(std::memory_order_acquire)) {
            // ready
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
            return;
        }

        // 
        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            //  set_value 
        } else {
            // 
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
        }
    }

    T await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
        return state_->value_;
    }

private:
    struct State {
        std::atomic<bool> ready_{false};
        T value_{};
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_; // value_exception_
    };

    std::shared_ptr<State> state_;
};

// AsyncPromisevoid
template<>
class AsyncPromise<void> {
public:
    AsyncPromise() : state_(std::make_shared<State>()) {}

    void set_value() {
        LOG_DEBUG("Setting AsyncPromise<void> value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->ready_.store(true, std::memory_order_release);

            // 
            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        // 
        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<void>");
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise<void> exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);

            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<void> exception");
            // 
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> h) {
        std::lock_guard<std::mutex> lock(state_->mutex_);

        if (state_->ready_.load(std::memory_order_acquire)) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
            return;
        }

        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            //  set_value 
        } else {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
        }
    }

    void await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
    }

private:
    struct State {
        std::atomic<bool> ready_{false};
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_;
    };

    std::shared_ptr<State> state_;
};

// AsyncPromiseunique_ptr
template<typename T>
class AsyncPromise<std::unique_ptr<T>> {
public:
    AsyncPromise() : state_(std::make_shared<State>()) {}

    void set_value(std::unique_ptr<T> value) {
        LOG_DEBUG("Setting AsyncPromise<unique_ptr> value");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->value_ = std::move(value);
            state_->ready_.store(true, std::memory_order_release);

            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<unique_ptr>");
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    void set_exception(std::exception_ptr ex) {
        LOG_DEBUG("Setting AsyncPromise<unique_ptr> exception");
        std::coroutine_handle<> handle_to_resume = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->exception_ = ex;
            state_->ready_.store(true, std::memory_order_release);

            handle_to_resume = state_->suspended_handle_.exchange(nullptr, std::memory_order_acq_rel);
        }

        if (handle_to_resume) {
            LOG_TRACE("Resuming waiting coroutine from AsyncPromise<unique_ptr> exception");
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle_to_resume);
        }
    }

    bool await_ready() {
        return state_->ready_.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> h) {
        std::lock_guard<std::mutex> lock(state_->mutex_);

        if (state_->ready_.load(std::memory_order_acquire)) {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
            return;
        }

        std::coroutine_handle<> expected = nullptr;
        if (state_->suspended_handle_.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
            // 
        } else {
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(h);
        }
    }

    std::unique_ptr<T> await_resume() {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->exception_) {
            std::rethrow_exception(state_->exception_);
        }
        return std::move(state_->value_);
    }

private:
    struct State {
        std::atomic<bool> ready_{false};
        std::unique_ptr<T> value_;
        std::exception_ptr exception_;
        std::atomic<std::coroutine_handle<>> suspended_handle_{nullptr};
        std::mutex mutex_;
    };

    std::shared_ptr<State> state_;
};

} // namespace flowcoro
