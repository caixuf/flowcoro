#pragma once
#include <mutex>
#include <vector>
#include <coroutine>

namespace flowcoro {

// RAII
class CoroutineScope {
public:
    ~CoroutineScope() {
        cancel_all();
    }

    void register_coroutine(std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cancelled_) {
            handles_.push_back(handle);
        }
    }

    void cancel_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        for (auto handle : handles_) {
            if (handle && !handle.done()) {
                handle.destroy();
            }
        }
        handles_.clear();
    }

    bool is_cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }

    void cleanup_completed() {
        std::lock_guard<std::mutex> lock(mutex_);
        handles_.erase(
            std::remove_if(handles_.begin(), handles_.end(),
                [](std::coroutine_handle<> h) { return !h || h.done(); }),
            handles_.end()
        );
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::coroutine_handle<>> handles_;
    bool cancelled_ = false;
};

} // namespace flowcoro
