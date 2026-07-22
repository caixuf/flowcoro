// =====================================================================
// flowcoro/c_api.h
//
// flowcoro 的 C ABI 封装。
//
// 目的：让纯 C 项目、其它语言 FFI（Rust cgo/cbindgen、Python ctypes、
// Go cgo）以及 C++ 项目的动态库边界，能在不直接接触 C++ 协程/模板/
// STL 的前提下，使用 flowcoro 的线程池、任务和内存池。
//
// 设计原则：
//   - 不透明句柄：所有 C++ 类型以 `flowcoro_*_t*` 形式暴露，C 端只拿指针
//   - C 端无 lambda：用 `flowcoro_task_fn`（函数指针 + void* user_data）替代
//   - 异常不跨边界：C++ 异常在 C 边界被 catch，转为错误码返回
//   - 显式 release：C 无 RAII，task 必须在 wait 后调用 release
//   - 显式 shutdown：C 端无静态析构顺序控制，提供 flowcoro_shutdown() 让
//     宿主在退出前主动回收线程池资源、drain hazard retired 节点
//
// 线程安全：除 flowcoro_task_t 句柄（同一 task 不可并发 wait）外，其它
// 句柄可被多线程并发使用。
// =====================================================================

#ifndef FLOWCORO_C_API_H
#define FLOWCORO_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------
// 错误码
// ---------------------------------------------------------------------

typedef enum {
    FLOWCORO_OK              = 0,
    FLOWCORO_ERROR_INVALID   = 1,  // 无效参数或句柄
    FLOWCORO_ERROR_SHUTDOWN  = 2,  // 线程池已停止
    FLOWCORO_ERROR_EXCEPTION = 3,  // 任务执行抛出 C++ 异常
    FLOWCORO_ERROR_TIMEOUT   = 4,  // 等待超时
    FLOWCORO_ERROR_ALLOC     = 5,  // 内存分配失败
} flowcoro_error_t;

// ---------------------------------------------------------------------
// 不透明句柄
// ---------------------------------------------------------------------

typedef struct flowcoro_pool_t flowcoro_pool_t;  // 线程池
typedef struct flowcoro_task_t flowcoro_task_t;  // 已提交任务（future 句柄）

// ---------------------------------------------------------------------
// 任务函数与回调签名
// ---------------------------------------------------------------------

// 任务函数：接收 user_data，返回 int（0=成功，非 0=用户自定义错误码）。
// 在 worker 线程执行；抛出的 C++ 异常会被捕获，task 结果变为
// FLOWCORO_ERROR_EXCEPTION。
typedef int (*flowcoro_task_fn)(void* user_data);

// user_data 释放回调：任务执行完成（无论成功/失败/异常）后调用一次，
// 用于释放 C 端传入的资源。可为 NULL（表示 user_data 无需释放）。
typedef void (*flowcoro_task_cleanup_fn)(void* user_data);

// ---------------------------------------------------------------------
// 线程池
// ---------------------------------------------------------------------

// 创建线程池。
//   num_threads: 工作线程数，0 表示按 hardware_concurrency 自动。
// 返回：线程池句柄，失败返回 NULL（极少见，仅当内部 new 失败）。
flowcoro_pool_t* flowcoro_pool_create(size_t num_threads);

// 析构线程池：等待所有 worker join，丢弃未处理任务。
// 析构后 pool 句柄不可再用。NULL 安全（传入 NULL 是 no-op）。
void flowcoro_pool_destroy(flowcoro_pool_t* pool);

// 运行时状态查询
size_t flowcoro_pool_active_threads(const flowcoro_pool_t* pool);
int    flowcoro_pool_is_stopped(const flowcoro_pool_t* pool);

// ---------------------------------------------------------------------
// 任务提交与等待
// ---------------------------------------------------------------------

// 提交任务到线程池。
//   pool:    线程池句柄
//   fn:      任务函数（不可为 NULL）
//   data:    传给 fn 的 user_data（可为 NULL）
//   cleanup: 任务完成后释放 data 的回调（可为 NULL）
// 返回：task 句柄，失败返回 NULL（pool 已 shutdown 或参数非法）。
//
// 生命周期：返回的 task 句柄必须最终调用 flowcoro_task_release() 释放，
// 否则会泄漏 future 共享状态。即使不 wait，也可 release 取消等待。
flowcoro_task_t* flowcoro_pool_submit(flowcoro_pool_t* pool,
                                       flowcoro_task_fn fn,
                                       void* data,
                                       flowcoro_task_cleanup_fn cleanup);

// 阻塞等待任务完成。
//   out_result: 可为 NULL；非 NULL 时写入 fn 的返回值。
// 返回：FLOWCORO_OK / FLOWCORO_ERROR_EXCEPTION / FLOWCORO_ERROR_INVALID。
// 多次 wait 同一 task：第一次返回真实结果，后续返回 FLOWCORO_ERROR_INVALID。
flowcoro_error_t flowcoro_task_wait(flowcoro_task_t* task, int* out_result);

// 非阻塞查询任务是否完成。1=完成，0=未完成，-1=无效句柄。
int flowcoro_task_is_done(flowcoro_task_t* task);

// 带超时等待。
//   timeout_ms: 超时毫秒数，0 表示不等待（等同于 is_done）。
// 返回：FLOWCORO_OK / FLOWCORO_ERROR_TIMEOUT / FLOWCORO_ERROR_INVALID。
flowcoro_error_t flowcoro_task_wait_for(flowcoro_task_t* task,
                                         uint64_t timeout_ms,
                                         int* out_result);

// 释放 task 句柄。必须最终调用一次。NULL 安全。
// 注意：release 不会取消正在执行的任务，仅释放 future 共享状态。
void flowcoro_task_release(flowcoro_task_t* task);

// ---------------------------------------------------------------------
// 内存池（直接包 SimpleMemoryPool）
// ---------------------------------------------------------------------

// 从 flowcoro 全局内存池分配。线程安全。
// 返回：分配的内存指针，失败返回 NULL。
void* flowcoro_alloc(size_t size);

// 归还内存到 flowcoro 全局内存池。ptr 可为 NULL（no-op）。
void flowcoro_free(void* ptr);

// ---------------------------------------------------------------------
// 全局关闭
// ---------------------------------------------------------------------

// 显式关闭：回收所有 hazard retired 节点，drain 全局 HazardManager。
// C 宿主在进程退出前调用，可避免静态析构顺序 fiasco（C 端无 C++ 静态
// 对象析构顺序保证）。多次调用安全。
//
// 调用后不应再使用 flowcoro_* 任何接口（包括未 release 的 task）。
void flowcoro_shutdown(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FLOWCORO_C_API_H
