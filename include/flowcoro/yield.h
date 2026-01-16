#pragma once
#include <coroutine>
#include "coroutine_manager.h"

namespace flowcoro {

// yield awaiter - 
struct YieldAwaiter {
    bool await_ready() const noexcept { 
        return false; // 
    }
    
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(h);
    }
    
    void await_resume() const noexcept {}
};

// 
inline YieldAwaiter yield() noexcept {
    return {};
}

// awaiter - yield
class BatchYieldAwaiter {
private:
    size_t& counter_;
    const size_t yield_interval_;
    
public:
    BatchYieldAwaiter(size_t& counter, size_t yield_interval = 100) 
        : counter_(counter), yield_interval_(yield_interval) {}
    
    bool await_ready() const noexcept {
        // 
        return (++counter_ % yield_interval_) != 0;
    }
    
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(h);
    }
    
    void await_resume() const noexcept {}
};

} // namespace flowcoro
