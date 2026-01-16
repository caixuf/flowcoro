#pragma once
#include "task.h"

namespace flowcoro {

// 
template<typename T>
T sync_wait(Task<T>&& task) {
    // Taskget()
    // get()get()
    return task.get();
}

// sync_waitLOG
inline void sync_wait(Task<void>&& task) {
    task.get(); // get()
}

//  - lambdaTask
template<typename Func>
auto sync_wait(Func&& func) {
    auto task = func();
    return sync_wait(std::move(task));
}

} // namespace flowcoro
