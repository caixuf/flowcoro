#pragma once
#include <functional>
#include <future>
#include "lockfree.h"

namespace flowcoro {

// 简化的全局线程池
class GlobalThreadPool {
private:
    static lockfree::ThreadPool& get_pool() {
        static lockfree::ThreadPool pool;
        return pool;
    }

public:
    static lockfree::ThreadPool& get() {
        return get_pool();
    }

    static bool is_shutdown_requested() {
        return false; // 简化实现
    }

    static void shutdown() {
        // 简化实现 - 线程池在程序结束时自动销毁
    }

    // 为了保持API兼容性，保留原有接口
    template<typename F, typename... Args>
    static auto enqueue(F&& f, Args&&... args) -> decltype(get().enqueue(std::forward<F>(f), std::forward<Args>(args)...)) {
        return get().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

    static void enqueue_void(std::function<void()> task) {
        get().enqueue_void(std::move(task));
    }
};

} // namespace flowcoro
