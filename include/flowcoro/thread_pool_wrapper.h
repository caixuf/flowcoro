#pragma once
#include "thread_pool.h"

namespace flowcoro {

/**
 * @brief 
 * 
 * 
 *  thread_pool.h  lockfree::ThreadPool
 */
class GlobalThreadPool {
public:
    /// 
    static lockfree::ThreadPool& get() {
        static lockfree::ThreadPool pool;
        return pool;
    }

    ///  future
    template<typename F, typename... Args>
    static auto enqueue(F&& f, Args&&... args) 
        -> decltype(get().enqueue(std::forward<F>(f), std::forward<Args>(args)...)) {
        return get().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

    /// 
    static void enqueue_void(std::function<void()> task) {
        get().enqueue_void(std::move(task));
    }

    /// 
    static bool is_shutdown_requested() {
        return get().is_stopped();
    }

    /// 
    static void shutdown() {
        get().shutdown();
    }

    /// 
    static size_t active_thread_count() {
        return get().active_thread_count();
    }
};

} // namespace flowcoro
