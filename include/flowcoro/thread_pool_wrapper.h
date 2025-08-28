#pragma once
#include "thread_pool.h"

namespace flowcoro {

/**
 * @brief 全局线程池适配器
 * 
 * 这是一个轻量级适配器，为项目提供统一的全局线程池访问接口。
 * 实际的线程池功能实现在 thread_pool.h 中的 lockfree::ThreadPool。
 */
class GlobalThreadPool {
public:
    /// 获取全局线程池实例
    static lockfree::ThreadPool& get() {
        static lockfree::ThreadPool pool;
        return pool;
    }

    /// 提交任务并返回 future
    template<typename F, typename... Args>
    static auto enqueue(F&& f, Args&&... args) 
        -> decltype(get().enqueue(std::forward<F>(f), std::forward<Args>(args)...)) {
        return get().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

    /// 提交无返回值任务
    static void enqueue_void(std::function<void()> task) {
        get().enqueue_void(std::move(task));
    }

    /// 检查是否已关闭
    static bool is_shutdown_requested() {
        return get().is_stopped();
    }

    /// 关闭线程池
    static void shutdown() {
        get().shutdown();
    }

    /// 获取活跃线程数
    static size_t active_thread_count() {
        return get().active_thread_count();
    }
};

} // namespace flowcoro
