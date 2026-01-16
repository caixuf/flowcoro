#pragma once
#include "safe_handle.h"
#include "coroutine_manager.h"

namespace flowcoro {

// safe_coroutine_handleresume
inline void safe_coroutine_handle::resume() {
    if (valid() && !handle_.done()) {
        // 
        auto& manager = CoroutineManager::get_instance();
        manager.schedule_resume(handle_);
    }
}

} // namespace flowcoro
