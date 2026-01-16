#pragma once
#include <coroutine>
#include "coroutine_manager.h"

namespace flowcoro {

// 轻量级yield awaiter - 立即恢复，但给其他协程执行机会
struct YieldAwaiter {
    bool await_ready() const noexcept { 
        return false; // 总是挂起以给其他协程机会
    }
    
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 立即重新调度当前协程，但允许其他协程先执行
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(h);
    }
    
    void await_resume() const noexcept {}
};

// 便捷函数：让出执行权
inline YieldAwaiter yield() noexcept {
    return {};
}

// 优化的批量操作awaiter - 在循环中周期性yield
class BatchYieldAwaiter {
private:
    size_t& counter_;
    const size_t yield_interval_;
    
public:
    BatchYieldAwaiter(size_t& counter, size_t yield_interval = 100) 
        : counter_(counter), yield_interval_(yield_interval) {}
    
    bool await_ready() const noexcept {
        // 只在达到间隔时才挂起
        return (++counter_ % yield_interval_) != 0;
    }
    
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(h);
    }
    
    void await_resume() const noexcept {}
};

} // namespace flowcoro
