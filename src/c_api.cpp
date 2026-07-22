// =====================================================================
// src/c_api.cpp
//
// flowcoro C ABI 实现。把 lockfree::ThreadPool、std::future、
// SimpleMemoryPool 包成 extern "C" 接口。
//
// 关键设计：
//   1) 句柄类型是 C++ 类型的别名（C 端只见 forward declaration 的指针）
//   2) 任务执行走 try/catch，异常转 FLOWCORO_ERROR_EXCEPTION
//   3) cleanup 通过 shared_ptr 自定义 deleter 保证恰好调用一次
//   4) future 的 wait 状态用 atomic 标记，防多次 wait
// =====================================================================

#include "flowcoro/c_api.h"

#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "flowcoro/memory_pool.h"      // pool_malloc / pool_free
#include "flowcoro/hazard_pointer.h"   // HazardManager::drain_all
#include "flowcoro/thread_pool.h"      // lockfree::ThreadPool

// ---------------------------------------------------------------------
// 句柄的 C++ 真身（对 C 端不透明）
// ---------------------------------------------------------------------

struct flowcoro_pool_t {
    lockfree::ThreadPool pool;
    explicit flowcoro_pool_t(size_t n) : pool(n) {}
};

struct flowcoro_task_t {
    std::future<int> future;
    std::atomic<bool> waited{false};  // 防多次 wait 返回不一致

    explicit flowcoro_task_t(std::future<int> f) : future(std::move(f)) {}
};

// ---------------------------------------------------------------------
// 线程池
// ---------------------------------------------------------------------

extern "C" {

flowcoro_pool_t* flowcoro_pool_create(size_t num_threads) {
    try {
        // num_threads=0 时 ThreadPool 内部用 hardware_concurrency
        auto* p = new flowcoro_pool_t(num_threads == 0
                                         ? std::thread::hardware_concurrency()
                                         : num_threads);
        return p;
    } catch (...) {
        return nullptr;
    }
}

void flowcoro_pool_destroy(flowcoro_pool_t* pool) {
    if (!pool) return;
    // ThreadPool 析构会 stop + join workers + 清空队列
    delete pool;
}

size_t flowcoro_pool_active_threads(const flowcoro_pool_t* pool) {
    return pool ? pool->pool.active_thread_count() : 0;
}

int flowcoro_pool_is_stopped(const flowcoro_pool_t* pool) {
    return pool ? (pool->pool.is_stopped() ? 1 : 0) : 1;
}

// ---------------------------------------------------------------------
// 任务提交
// ---------------------------------------------------------------------

flowcoro_task_t* flowcoro_pool_submit(flowcoro_pool_t* pool,
                                       flowcoro_task_fn fn,
                                       void* data,
                                       flowcoro_task_cleanup_fn cleanup) {
    if (!pool || !fn) return nullptr;
    if (pool->pool.is_stopped()) return nullptr;

    // TaskContext 持有任务执行所需的一切。cleanup 必须恰好调用一次：
    //   - 正常路径：worker 执行完 fn 后立即调（不依赖 packaged_task 析构时机）
    //   - 兜底路径：ctx 析构时（任务未执行就被丢弃，如 pool shutdown）调
    // compare_exchange 防重复。
    struct TaskContext {
        flowcoro_task_fn fn;
        void* data;
        flowcoro_task_cleanup_fn cleanup;
        std::atomic<bool> cleanup_done{false};

        ~TaskContext() {
            // 兜底：任务未执行就被丢弃时调
            bool expected = false;
            if (cleanup && data &&
                cleanup_done.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                cleanup(data);
            }
        }
    };

    auto ctx = std::make_shared<TaskContext>();
    ctx->fn = fn;
    ctx->data = data;
    ctx->cleanup = cleanup;

    try {
        // enqueue 返回 std::future<int>。ctx 通过 shared_ptr 捕获 by-value，
        // 任务执行后立即调 cleanup（主路径），析构时兜底（丢弃路径）。
        std::future<int> fut = pool->pool.enqueue(
            [ctx]() -> int {
                int result;
                try {
                    result = ctx->fn(ctx->data);
                } catch (const std::exception&) {
                    result = FLOWCORO_ERROR_EXCEPTION;
                } catch (...) {
                    result = FLOWCORO_ERROR_EXCEPTION;
                }
                // 立即调 cleanup（任务执行后）
                bool expected = false;
                if (ctx->cleanup && ctx->data &&
                    ctx->cleanup_done.compare_exchange_strong(
                        expected, true, std::memory_order_acq_rel)) {
                    ctx->cleanup(ctx->data);
                }
                return result;
            });

        auto* task = new flowcoro_task_t(std::move(fut));
        return task;
    } catch (const std::runtime_error&) {
        // ThreadPool::enqueue 在 shutdown 时抛 runtime_error
        // 注意：此时 ctx 仍会通过 shared_ptr 析构兜底调 cleanup
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

// ---------------------------------------------------------------------
// 任务等待
// ---------------------------------------------------------------------

flowcoro_error_t flowcoro_task_wait(flowcoro_task_t* task, int* out_result) {
    if (!task) return FLOWCORO_ERROR_INVALID;

    // CAS 防多次 wait：第一次 wait 设为 true
    bool expected = false;
    if (!task->waited.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return FLOWCORO_ERROR_INVALID;  // 已被 wait 过
    }

    try {
        int r = task->future.get();  // 阻塞等待并取结果
        if (out_result) *out_result = r;
        return FLOWCORO_OK;
    } catch (...) {
        // 极少见：future 已被 get 过、或 promise 抛 broken_promise
        return FLOWCORO_ERROR_EXCEPTION;
    }
}

int flowcoro_task_is_done(flowcoro_task_t* task) {
    if (!task) return -1;
    // 已 wait 过（get() 已执行）必然已完成。必须先检查 waited，否则
    // future.wait_for 在已 get 的 future 上会抛 future_error。
    if (task->waited.load(std::memory_order_acquire)) return 1;
    // future 状态查询不消耗结果，可多次调用
    using namespace std::chrono;
    return task->future.wait_for(0ms) == std::future_status::ready ? 1 : 0;
}

flowcoro_error_t flowcoro_task_wait_for(flowcoro_task_t* task,
                                         uint64_t timeout_ms,
                                         int* out_result) {
    if (!task) return FLOWCORO_ERROR_INVALID;

    bool expected = false;
    if (!task->waited.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return FLOWCORO_ERROR_INVALID;
    }

    using namespace std::chrono;
    auto status = task->future.wait_for(milliseconds(timeout_ms));
    if (status != std::future_status::ready) {
        // 超时：回退 waited 标记，允许后续再 wait/wait_for
        task->waited.store(false, std::memory_order_release);
        return FLOWCORO_ERROR_TIMEOUT;
    }

    try {
        int r = task->future.get();
        if (out_result) *out_result = r;
        return FLOWCORO_OK;
    } catch (...) {
        return FLOWCORO_ERROR_EXCEPTION;
    }
}

void flowcoro_task_release(flowcoro_task_t* task) {
    if (!task) return;
    // 注意：release 不等待任务执行。若任务尚未完成，future 析构会
    // 等待 promise 满足（即任务执行完）才释放共享状态。
    // C 端若要"取消"任务而非等待，需自行设计——C++ future 本无取消语义。
    delete task;
}

// ---------------------------------------------------------------------
// 内存池
// ---------------------------------------------------------------------

void* flowcoro_alloc(size_t size) {
    return flowcoro::pool_malloc(size);
}

void flowcoro_free(void* ptr) {
    flowcoro::pool_free(ptr);
}

// ---------------------------------------------------------------------
// 全局关闭
// ---------------------------------------------------------------------

void flowcoro_shutdown(void) {
    // drain 所有线程的 hazard retired 节点，避免静态析构顺序 fiasco
    // 下导致 ~Queue() 在 HazardManager 析构后访问 retired vector。
    flowcoro::smr::HazardManager::instance().drain_all();
}

}  // extern "C"
