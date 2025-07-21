#pragma once

#include <coroutine>
#include <tuple>
#include <vector>
#include <memory>
#include <atomic>
#include <exception>
#include <optional>
#include <any>
#include "core.h"

namespace flowcoro {

namespace detail {
    // Type-erased result for when_any and when_race
    struct AnyResult {
        size_t index;
        std::any value;
        
        template<typename T>
        T get() const {
            return std::any_cast<T>(value);
        }
    };
    
    // Type trait to extract T from Task<T>
    template<typename T>
    struct task_value_type {};
    
    template<typename T>
    struct task_value_type<Task<T>> {
        using type = T;
    };
    
    template<typename T>
    using task_value_type_t = typename task_value_type<T>::type;
}

// Shared state for synchronization
template<typename... Tasks>
struct SharedState {
    std::atomic<bool> completed{false};
    std::atomic<size_t> completed_count{0};
    size_t completed_index{0};
    std::vector<std::any> results_;
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    
    SharedState() : results_(sizeof...(Tasks)) {}
};

// WhenAny awaiter
template<typename... Tasks>
class WhenAnyAwaiter {
public:
    explicit WhenAnyAwaiter(Tasks&&... tasks) : tasks_(std::forward<Tasks>(tasks)...) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        shared_state_ = std::make_shared<SharedState<Tasks...>>();
        shared_state_->continuation_ = handle;
        
        auto start_task = [this](auto& task, size_t index) {
            GlobalThreadPool::get().enqueue_void([this, task_ref = std::ref(task), index]() mutable {
                try {
                    if constexpr (std::is_void_v<detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>>) {
                        task_ref.get().get();
                        if (!shared_state_->completed.exchange(true)) {
                            shared_state_->completed_index = index;
                            shared_state_->results_[index] = std::monostate{};
                            shared_state_->continuation_.resume();
                        }
                    } else {
                        auto result = task_ref.get().get();
                        if (!shared_state_->completed.exchange(true)) {
                            shared_state_->completed_index = index;
                            shared_state_->results_[index] = std::move(result);
                            shared_state_->continuation_.resume();
                        }
                    }
                } catch (...) {
                    if (!shared_state_->completed.exchange(true)) {
                        shared_state_->exception_ = std::current_exception();
                        shared_state_->continuation_.resume();
                    }
                }
            });
        };

        size_t index = 0;
        std::apply([&](auto&... tasks) {
            (start_task(tasks, index++), ...);
        }, tasks_);
    }

    detail::AnyResult await_resume() {
        if (shared_state_->exception_) {
            std::rethrow_exception(shared_state_->exception_);
        }
        
        detail::AnyResult result;
        result.index = shared_state_->completed_index;
        result.value = shared_state_->results_[shared_state_->completed_index];
        return result;
    }

private:
    std::tuple<Tasks...> tasks_;
    std::shared_ptr<SharedState<Tasks...>> shared_state_;
};

// WhenRace awaiter (similar to WhenAny)
template<typename... Tasks>  
class WhenRaceAwaiter {
public:
    explicit WhenRaceAwaiter(Tasks&&... tasks) : tasks_(std::forward<Tasks>(tasks)...) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        shared_state_ = std::make_shared<SharedState<Tasks...>>();
        shared_state_->continuation_ = handle;
        
        auto start_task = [this](auto& task, size_t task_index) {
            GlobalThreadPool::get().enqueue_void([shared_state = shared_state_, task_ref = std::ref(task), task_index]() mutable {
                try {
                    if constexpr (std::is_void_v<detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>>) {
                        task_ref.get().get();
                        if (!shared_state->completed.exchange(true)) {
                            shared_state->completed_index = task_index;
                            shared_state->results_[task_index] = std::monostate{};
                            shared_state->continuation_.resume();
                        }
                    } else {
                        auto result = task_ref.get().get();
                        if (!shared_state->completed.exchange(true)) {
                            shared_state->completed_index = task_index;
                            shared_state->results_[task_index] = result;
                            shared_state->continuation_.resume();
                        }
                    }
                } catch (...) {
                    if (!shared_state->completed.exchange(true)) {
                        shared_state->exception_ = std::current_exception();
                        shared_state->continuation_.resume();
                    }
                }
            });
        };

        std::apply([&](auto&... tasks) {
            size_t index = 0;
            (start_task(tasks, index++), ...);
        }, tasks_);
    }

    detail::AnyResult await_resume() {
        if (shared_state_->exception_) {
            std::rethrow_exception(shared_state_->exception_);
        }
        
        detail::AnyResult result;
        result.index = shared_state_->completed_index;
        result.value = shared_state_->results_[shared_state_->completed_index];
        return result;
    }

private:
    std::tuple<Tasks...> tasks_;
    std::shared_ptr<SharedState<Tasks...>> shared_state_;
};

// Task result wrapper for when_all_settled
template<typename T>
struct TaskResult {
    bool success;
    std::optional<T> value;
    std::exception_ptr exception;
    
    TaskResult() : success(false) {}
    explicit TaskResult(T&& val) : success(true), value(std::move(val)) {}
    explicit TaskResult(std::exception_ptr ex) : success(false), exception(ex) {}
};

template<>
struct TaskResult<void> {
    bool success;
    std::exception_ptr exception;
    
    TaskResult() : success(false) {}
    explicit TaskResult(bool succeeded) : success(succeeded) {}
    explicit TaskResult(std::exception_ptr ex) : success(false), exception(ex) {}
};

// WhenAllSettled awaiter
template<typename... Tasks>
class WhenAllSettledAwaiter {
public:
    explicit WhenAllSettledAwaiter(Tasks&&... tasks) : tasks_(std::forward<Tasks>(tasks)...) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        shared_state_ = std::make_shared<SharedState<Tasks...>>();
        shared_state_->continuation_ = handle;
        results_.resize(sizeof...(Tasks));
        
        auto start_task = [this](auto& task, size_t task_index) {
            GlobalThreadPool::get().enqueue_void([this, task_ref = std::ref(task), task_index]() mutable {
                try {
                    if constexpr (std::is_void_v<detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>>) {
                        task_ref.get().get();
                        set_result(task_index, TaskResult<void>(true));
                    } else {
                        auto result = task_ref.get().get();
                        using TaskType = detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>;
                        set_result(task_index, TaskResult<TaskType>(std::move(result)));
                    }
                } catch (...) {
                    if constexpr (std::is_void_v<detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>>) {
                        set_result(task_index, TaskResult<void>(std::current_exception()));
                    } else {
                        using TaskType = detail::task_value_type_t<std::remove_reference_t<decltype(task_ref.get())>>;
                        set_result(task_index, TaskResult<TaskType>(std::current_exception()));
                    }
                }
            });
        };

        std::apply([&](auto&... tasks) {
            size_t index = 0;
            (start_task(tasks, index++), ...);
        }, tasks_);
    }

    std::vector<std::any> await_resume() {
        return results_;
    }

private:
    template<typename ResultType>
    void set_result(size_t index, ResultType&& result) {
        results_[index] = std::make_any<ResultType>(std::forward<ResultType>(result));
        if (shared_state_->completed_count.fetch_add(1) + 1 == sizeof...(Tasks)) {
            shared_state_->continuation_.resume();
        }
    }
    
    std::vector<std::any> results_;
    std::tuple<Tasks...> tasks_;
    std::shared_ptr<SharedState<Tasks...>> shared_state_;
};

// Factory functions
template<typename... Tasks>
WhenAnyAwaiter<Tasks...> when_any(Tasks&&... tasks) {
    return WhenAnyAwaiter<Tasks...>(std::forward<Tasks>(tasks)...);
}

template<typename... Tasks>
WhenRaceAwaiter<Tasks...> when_race(Tasks&&... tasks) {
    return WhenRaceAwaiter<Tasks...>(std::forward<Tasks>(tasks)...);
}

template<typename... Tasks>
WhenAllSettledAwaiter<Tasks...> when_all_settled(Tasks&&... tasks) {
    return WhenAllSettledAwaiter<Tasks...>(std::forward<Tasks>(tasks)...);
}

} // namespace flowcoro
