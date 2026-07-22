#pragma once
#include <tuple>
#include <any>
#include <memory>
#include <atomic>
#include <utility>
#include <type_traits>
#include <exception>
#include "task.h"
#include "awaiter.h"
#include "coroutine_manager.h"

namespace flowcoro {

// =====================================================================
// when_all 实现 - 等待所有 task 完成（保留原实现）
// =====================================================================
template<typename... Tasks>
Task<std::tuple<decltype(std::declval<Tasks>().get())...>> when_all(Tasks&&... tasks) {
    auto execute_all = []<typename... Ts>(Ts&&... ts) -> Task<std::tuple<decltype(ts.get())...>> {
        std::tuple<decltype(ts.get())...> results;
        auto await_task = [&]<std::size_t... Is>(std::index_sequence<Is...>) -> Task<void> {
            ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
            co_return;
        };
        co_await await_task(std::index_sequence_for<Ts...>{});
        co_return std::move(results);
    };
    co_return co_await execute_all(std::forward<Tasks>(tasks)...);
}

namespace detail {

// =====================================================================
// when_any 共享状态
// 由 when_any 调用者和 N 个 selector 协程共享。
// 通过 CAS 决出唯一胜者，避免数据竞争。
// =====================================================================
struct WhenAnyState {
    std::atomic<bool> winner_set{false};
    std::atomic<std::size_t> winner_index{static_cast<std::size_t>(-1)};
    std::any result;
    // 等待者协程地址，由 selector 在胜出后通过 exchange 取走并调度恢复
    std::atomic<void*> waiter{nullptr};

    // 原子地尝试成为胜者
    bool try_win(std::size_t index) noexcept {
        bool expected = false;
        if (winner_set.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            winner_index.store(index, std::memory_order_release);
            return true;
        }
        return false;
    }

    // 唤醒等待者（若已注册）
    void resume_waiter() noexcept {
        void* w = waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (w) {
            auto h = std::coroutine_handle<>::from_address(w);
            // 通过管理器调度恢复，避免直接 resume 触发栈溢出
            flowcoro::CoroutineManager::get_instance().schedule_resume(h);
        }
    }
};

// =====================================================================
// selector 协程
// 持有并 await 一个原始 task，完成后尝试 CAS 成为胜者
// =====================================================================
template<std::size_t Index, typename TaskT>
Task<void> selector(TaskT task, std::shared_ptr<WhenAnyState> state) {
    using R = std::decay_t<decltype(std::declval<TaskT>().get())>;
    try {
        if constexpr (std::is_void_v<R>) {
            co_await std::move(task);
            if (state->try_win(Index)) {
                state->result = std::any{};
                state->resume_waiter();
            }
        } else {
            auto val = co_await std::move(task);
            if (state->try_win(Index)) {
                state->result = std::make_any<R>(std::move(val));
                state->resume_waiter();
            }
        }
    } catch (...) {
        // 异常也视为完成：把异常存入 any
        if (state->try_win(Index)) {
            state->result = std::make_any<std::exception_ptr>(std::current_exception());
            state->resume_waiter();
        }
    }
    co_return;
}

// =====================================================================
// when_any 的等待器
// 在 await_suspend 中注册 caller，然后原子地处理 "selector 先赢" 的竞态
// =====================================================================
struct WhenAnyAwaiter {
    std::shared_ptr<WhenAnyState> state;

    bool await_ready() const noexcept {
        // 若已有胜者，直接走 await_resume 不挂起
        return state->winner_set.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        // 注册 caller 为等待者
        state->waiter.store(caller.address(), std::memory_order_release);

        // 防止 selector 在 await_ready 与 await_suspend 之间胜出：
        // 再次检查 winner_set，若已赢则尝试自行唤醒
        if (state->winner_set.load(std::memory_order_acquire)) {
            void* expected = caller.address();
            if (state->waiter.compare_exchange_strong(
                    expected, nullptr, std::memory_order_acq_rel)) {
                // selector 没机会唤醒我们，自行调度
                flowcoro::CoroutineManager::get_instance().schedule_resume(caller);
            }
            // 否则 selector 正在唤醒我们，无需操作
        }
        // 否则等待 selector 唤醒
    }

    void await_resume() const noexcept {}
};

} // namespace detail

// =====================================================================
// when_any 实现 - 事件驱动版本（替代轮询）
//
// 原实现使用 1ms 轮询 (co_await ClockAwaiter(1ms))，存在两个问题：
//   1) 延迟下限 1ms — 实时性差
//   2) CPU 空转 — 任务密集时浪费 cycles
//
// 修复：为每个 task 创建一个 selector 协程。selector 持有 task
// 并 co_await 它；任何 task 完成即立刻唤醒其 selector，selector 通过
// CAS 决出胜者，胜者立刻 schedule_resume(when_any caller)。延迟接近 0，
// 无轮询，无 CPU 空转。
//
// 落败的 selector 在 when_any 返回时被其 Task 析构函数安全销毁
// （Task::safe_destroy 会调用 manager.schedule_destroy 延迟回收）。
// =====================================================================
template<typename... Tasks>
Task<std::pair<std::size_t, std::any>> when_any(Tasks&&... tasks) {
    auto state = std::make_shared<detail::WhenAnyState>();

    // 用 lambda 创建 N 个 selector，存储于 tuple 中作为本协程局部变量
    auto launch = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::make_tuple(
            detail::selector<Is>(
                std::forward<Tasks>(tasks), state)...
        );
    };
    auto selectors = launch(std::index_sequence_for<Tasks...>{});

    // 事件等待：若有 selector 在 await_ready 之前已胜出，则不挂起
    co_await detail::WhenAnyAwaiter{state};

    // 读出胜者信息
    auto winner_idx = state->winner_index.load(std::memory_order_acquire);

    // 在 co_return 之前，把所有 selector Task 显式移交给
    // CoroutineManager 延迟销毁。这里不调用 selector.cancel() 或
    // handle.done()——两者都会跨线程访问 selector 协程帧，触发 TSAN
    // data race。改为通过 detach（置空 handle）放弃所有权，让 selector
    // 协程自然完成并挂起在 final_suspend，之后由 CoroutineManager 的
    // 定期清理或进程退出处理。
    //
    // 注意：此处只是把 Task 的 handle 置空，不触发 safe_destroy。
    // selector 协程帧仍由 CoroutineManager 管理（selector 协程完成时
    // 会调用 resume_waiter → schedule_resume caller，自身挂起在
    // final_suspend，帧不会被自动回收）。这是有意的——在协程池模式下
    // 这些帧会在进程退出时被统一清理。
    auto detach_selectors = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        // 把 selector Task 的 handle 置空，避免 Task 析构时调用
        // safe_destroy → handle.done() 的跨线程读取
        ((std::get<Is>(selectors).detach()), ...);
    };
    detach_selectors(std::index_sequence_for<Tasks...>{});

    co_return std::make_pair(winner_idx, std::move(state->result));
}

// =====================================================================
// 便捷重载：when_any_timeout
// 将超时也建模为一个 task，与原任务做 when_any race。
// 胜者索引 0 -> 原任务完成；胜者索引 1 -> 超时。
// =====================================================================
template<typename TaskType, typename Rep, typename Period>
auto when_any_timeout(TaskType&& task, std::chrono::duration<Rep, Period> timeout) {
    auto timeout_task = [timeout]() -> Task<bool> {
        co_await ClockAwaiter(
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
        co_return false; // 超时返回 false
    }();

    return when_any(std::forward<TaskType>(task), std::move(timeout_task));
}

} // namespace flowcoro
