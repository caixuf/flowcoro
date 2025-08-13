#pragma once
#include "safe_handle.h"
#include "coroutine_manager.h"

namespace flowcoro {

// safe_coroutine_handle的resume实现
inline void safe_coroutine_handle::resume() {
    if (valid() && !handle_.done()) {
        // 使用协程管理器统一调度
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(handle_);
    }
}

} // namespace flowcoro
