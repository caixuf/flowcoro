#pragma once
#include "task.h"

namespace flowcoro {

// 同步等待协程完成的函数
template<typename T>
T sync_wait(Task<T>&& task) {
    // 直接使用Task的get()方法，它会阻塞直到完成
    // 不使用异常，直接返回get()的结果（get()内部已处理错误并返回默认值）
    return task.get();
}

// sync_wait需要特殊处理，避免LOG调用
inline void sync_wait(Task<void>&& task) {
    task.get(); // 不使用异常，get()内部已处理错误并记录日志
}

// 重载版本 - 接受lambda并返回Task
template<typename Func>
auto sync_wait(Func&& func) {
    auto task = func();
    return sync_wait(std::move(task));
}

} // namespace flowcoro
