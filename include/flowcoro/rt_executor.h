/**
 * @file rt_executor.h
 * @brief flowcoro::rt — 确定性实时执行模型
 *
 * 设计目标: 单线程亲和、周期执行、销毁只在静止点、稳态调度零分配(内部重投递
 * 走 vector 快照);跨线程 post_ready 用池节点(预热后复用,池空退 std::malloc)、标志式取消。
 * 任何机器人/控制/嵌入式项目都想要的通用实时执行模型。它是纯新增, 不碰
 * Task<T>/CoroTask, 零回归面; 有明确消费者(FlowEngine)。
 *
 * =====================================================================
 * 核心正确性契约(一切皆压在此句之上):
 *
 *   所有 resume 和 destroy 都发生在 run() 的调用线程(executor 线程)上。
 *   事件在别的线程发生时, 只能 post_ready(h) 把 handle 递回来,
 *   绝不 inline h.resume()。
 *
 *   这是 FlowEngine 现在做错(在别的线程 inline resume)、导致 M1 的那一点。
 * =====================================================================
 *
 * 使用约定:
 *   - spawn() 与 run() 必须在同一(宿主)线程调用, 且不并发。
 *   - post_ready() / request_stop() 可从任意线程调用(无锁队列 / 原子)。
 *   - 宿主每个 tick 调一次 run(); 持续调用直到 is_finished()。
 *
 * 事件流(双队列 + tick 快照):
 *   - 内部重投递(spawn/yield/final/timer)走 local_ready_(executor 线程私有 vector,
 *     稳态零分配)。
 *   - 跨线程事件走 ready_ext_(MPSC 无锁, post_ready)。
 *   run() = 非阻塞 tick。每次调用:
 *     1) process_timers: 到期 timer 的 handle -> local_ready_(绝不在 timer 路径 resume);
 *        若 stop 已请求, 取消全部剩余 timer -> local_ready_。
 *     2) tick 边界快照: tick_batch_.clear() + swap 自 local_ready_ + 抽干 ready_ext_。
 *        处理中新产生的 yield/final 落进"新的" local_ready_(下 tick 才处理) ——
 *        这从根上杜绝 yield 在单 tick 内无限重入(R1 正解)。
 *     3) 遍历 tick_batch_: done 帧 -> destroy(executor 线程), 其余 -> resume。
 *   不阻塞、不 notify、不 syscall。
 *
 * 两段式拆除:
 *   request_stop() -> 置标志
 *     -> 下一次 run() 取消 timer(推 local_ready_)
 *     -> task 在周期边界查 stop 后 co_return 到 final_suspend(park, 标 done, 推 local_ready_)
 *     -> 同一次/下一次 run() 的 drain 把 done 帧在 executor 线程 destroy
 *     -> active 空 => is_finished()
 *   优雅关停请用 shutdown(); 析构仅为兜底(见 ~RtExecutor 注释)。
 */

#pragma once

#include <coroutine>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <functional>
#include <exception>
#include <stdexcept>
#include <thread>

#include "flowcoro/lockfree.h"
#include "flowcoro/logger.h"

namespace flowcoro::rt {

class RtExecutor;

// ---------------------------------------------------------------------------
// StopToken: 协作式停止令牌, 在周期边界查, 不抢占。
// 持有指向 executor 内 stop_flag_ 的指针; executor 保证比所有 token 长命。
// ---------------------------------------------------------------------------
class StopToken {
    const std::atomic<bool>* flag_ = nullptr;
public:
    StopToken() = default;
    explicit StopToken(const std::atomic<bool>& f) noexcept : flag_(&f) {}
    bool requested() const noexcept {
        return flag_ && flag_->load(std::memory_order_acquire);
    }
};

// ---------------------------------------------------------------------------
// RtTask: 确定性任务。
//   - 惰性启动 (initial_suspend = suspend_always)
//   - 长生命周期, owner 唯一 (executor)
//   - 协程 co_return void
//   - 析构不直接销毁帧 —— 帧的生命周期归 executor(两段式拆除)
// ---------------------------------------------------------------------------
class [[nodiscard]] RtTask {
public:
    struct promise_type;

    using handle_type = std::coroutine_handle<promise_type>;

    RtTask() = default;
    explicit RtTask(handle_type h) noexcept : handle_(h) {}
    RtTask(RtTask&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
    RtTask& operator=(RtTask&& o) noexcept {
        if (this != &o) {
            if (handle_) handle_.destroy();
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    RtTask(const RtTask&) = delete;
    RtTask& operator=(const RtTask&) = delete;

    // 若未被 spawn 接管, 销毁帧避免泄漏; spawn 接管后 handle_ 已被 release() 置空。
    ~RtTask() {
        if (handle_) {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    // 转移所有权: 取出 handle 并置空, 使析构成为 no-op。供 executor::spawn 使用。
    [[nodiscard]] handle_type release() noexcept {
        auto h = handle_;
        handle_ = nullptr;
        return h;
    }

    handle_type handle() const noexcept { return handle_; }

private:
    handle_type handle_ = nullptr;
};

// ---------------------------------------------------------------------------
// RtTask::promise_type
//   - initial_suspend = suspend_always (惰性)
//   - final_suspend    = park (标 done, 推 ready; 绝不被 resume, 由 run() destroy)
//   - unhandled_exception = 记日志 + 标 dead, executor 继续(确定性优先)
//
// done_ / dead_ / prev_ / next_ 仅在 executor 线程访问:
//   final_awaiter::await_suspend 在 executor 线程(resume 栈)中执行, 设置 done_ 并
//   通过 post_ready 把 handle 递回 ready 队列。run() 的 drain 读 done_ 决定 resume
//   还是 destroy。MPSC ready 队列的 release/acquire 已足够保证可见性, 故这里用
//   普通 bool 而非 atomic。
// ---------------------------------------------------------------------------
struct RtTask::promise_type {
    RtExecutor* executor_ = nullptr;
    std::string name_;
    bool done_ = false;          // 已到达 final_suspend(park)
    bool dead_ = false;         // 抛过未捕获异常
    promise_type* prev_ = nullptr;  // 侵入式任务链表(仅 executor 线程)
    promise_type* next_ = nullptr;

    RtTask get_return_object() {
        return RtTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() noexcept { return false; }  // 永远 park
        // out-of-line 定义: 需要 RtExecutor 完整(调用 post_ready)
        void await_suspend(std::coroutine_handle<promise_type> h) noexcept;
        void await_resume() noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        dead_ = true;
        try {
            if (auto ep = std::current_exception()) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            LOG_ERROR("RtTask '%s' unhandled exception: %s",
                      name_.c_str(), e.what());
        } catch (...) {
            LOG_ERROR("RtTask '%s' unhandled exception (unknown)",
                      name_.c_str());
        }
    }

    // awaitable 入口: 取出所属 executor(供 sleep_for / yield / stop_requested)。
    RtExecutor* rt_executor() const noexcept { return executor_; }

    friend class RtExecutor;
};

// ---------------------------------------------------------------------------
// RtExecutor: 单线程确定性执行器。
// ---------------------------------------------------------------------------
class RtExecutor {
public:
    struct Config {
        int pin_cpu = -1;               // CPU 亲和性(run_blocking 生效); -1 不绑定
        uint64_t idle_sleep_us = 1000;   // run_blocking 空闲 tick 间睡眠
    };

    using Handle = std::coroutine_handle<>;

private:
    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline;
        Handle handle;
    };

    // 跨线程 ready 队列: MPSC 无锁。仅接收跨线程 post_ready。
    // 内部重投递(spawn/yield/final/timer)走 local_ready_(见下),避免热路径分配
    // 且让 yield 等推迟到下一个 tick(见 run())。
    lockfree::Queue<Handle> ready_ext_;

    // executor 线程私有: 内部重投递(spawn/yield/final/timer)。非线程安全。
    std::vector<Handle> local_ready_;

    // 每 tick 复用的处理快照: run() 入口 swap 自 local_ready_ + 抽干 ready_ext_。
    // swap 保留 capacity,稳态零重分配。
    std::vector<Handle> tick_batch_;

    // 定时器最小堆: 仅 executor 线程访问(sleep_for 的 await_suspend 在 run() 的
    // resume 栈上跑)。无需锁。
    std::vector<TimerEntry> timers_;

    // 侵入式活跃任务链表(仅 executor 线程): 析构时回收仍在 parked 的帧。
    RtTask::promise_type* task_list_head_ = nullptr;

    std::atomic<bool> stop_flag_{false};
    std::atomic<size_t> active_count_{0};
    Config config_;

    // 定时器堆比较: 最小堆(parent <= children), std::greater<TimerEntry> 用 operator>。
    static bool timer_less(const TimerEntry& a, const TimerEntry& b) noexcept {
        return a.deadline > b.deadline;
    }

public:
    explicit RtExecutor() : config_() {}
    explicit RtExecutor(Config cfg) : config_(cfg) {}
    ~RtExecutor();

    RtExecutor(const RtExecutor&) = delete;
    RtExecutor& operator=(const RtExecutor&) = delete;

    // 注册任务。帧 park 着, executor 接管所有权。必须与 run() 同线程、不并发。
    void spawn(RtTask task, std::string_view name);

    // 非阻塞 tick: 处理到期/取消 timer + 抽干 ready(resume 或 destroy)。宿主每 tick 调一次。
    void run();

    // 阻塞便捷模式: 在本线程上 loop run() + idle 睡眠, 直到 is_finished()。
    // 若 config_.pin_cpu >= 0, 绑定当前线程到该 CPU(Linux)。
    void run_blocking();

    // 任意线程可调; 置停止标志。下一次 run() 会排空 timer(推 local_ready_),
    // task 在周期边界查 stop 后 co_return, run() 在 executor 线程 destroy done 帧。
    void request_stop() noexcept {
        stop_flag_.store(true, std::memory_order_release);
    }

    // 优雅关停(R3 正解)。必须在 executor 线程调, 且所有会 post_ready 的外部生产者
    // 已 join。request_stop -> 反复 run() 直到所有帧 co_return 并在 executor 线程被
    // 销毁(quiesce)。若存在仍等外部 post_ready 的 parked 帧且生产者未 join,会无限
    // 循环 —— 这是调用方违反前置条件的责任。
    void shutdown() {
        request_stop();
        while (!is_finished()) run();
    }

    StopToken stop_token() const noexcept {
        return StopToken{stop_flag_};
    }

    bool stop_requested() const noexcept {
        return stop_flag_.load(std::memory_order_acquire);
    }

    // 跨线程事件唯一合法出口: 把 handle 压入 ready_ext_。
    // 不 notify、不 syscall、不 inline resume。
    // 契约: 每个 parked 帧只能有唯一恢复源;禁止对同一 handle 重复/陈旧 post_ready,
    // 否则该帧被 destroy 后二次出队 = UAF。
    void post_ready(Handle h) noexcept { ready_ext_.enqueue(h); }

    // executor 线程内部重投递(yield/final_suspend/timer/spawn)。非线程安全,
    // 只能在 run() 调用线程用。走 vector,稳态零分配。落入的 handle 在下一个 tick
    // 才被处理(见 run() 的 tick 快照),从根上杜绝 yield 在单 tick 内无限重入。
    void post_local(Handle h) { local_ready_.push_back(h); }

    // 所有已 spawn 的帧都已 destroy(quiesced)。宿主据此停止 tick。
    // 周期任务在 request_stop 后才会 co_return 到 active==0; 一次性任务自然到达。
    bool is_finished() const noexcept {
        return active_count_.load(std::memory_order_acquire) == 0;
    }

private:
    // 仅 executor 线程: 注册定时器(sleep_for 的 await_suspend 调用)。
    void register_timer(std::chrono::steady_clock::time_point deadline, Handle h) {
        timers_.push_back(TimerEntry{deadline, h});
        std::push_heap(timers_.begin(), timers_.end(), timer_less);
    }

    // 仅 executor 线程: 处理到期 timer + stop 时取消全部 timer。
    void process_timers() {
        auto now = std::chrono::steady_clock::now();
        while (!timers_.empty() && timers_.front().deadline <= now) {
            std::pop_heap(timers_.begin(), timers_.end(), timer_less);
            Handle h = timers_.back().handle;
            timers_.pop_back();
            post_local(h);  // 绝不在 timer 路径 resume; 走 local_ready_, 本 tick 处理
        }
        if (stop_flag_.load(std::memory_order_acquire)) {
            for (auto& e : timers_) post_local(e.handle);
            timers_.clear();
        }
    }

    // 任务链表(仅 executor 线程)
    void task_list_push(RtTask::promise_type& p) noexcept {
        p.prev_ = nullptr;
        p.next_ = task_list_head_;
        if (task_list_head_) task_list_head_->prev_ = &p;
        task_list_head_ = &p;
    }
    void task_list_unlink(RtTask::promise_type& p) noexcept {
        if (p.prev_) p.prev_->next_ = p.next_;
        else task_list_head_ = p.next_;
        if (p.next_) p.next_->prev_ = p.prev_;
        p.prev_ = p.next_ = nullptr;
    }
    void destroy_all_tasks() noexcept {
        auto* p = task_list_head_;
        task_list_head_ = nullptr;
        while (p) {
            auto* next = p->next_;  // 必须在 destroy 之前读 next
            std::coroutine_handle<RtTask::promise_type>::from_promise(*p).destroy();
            p = next;
        }
    }

    friend struct RtTask::promise_type::final_awaiter;
    friend struct sleep_awaiter;  // 仅 sleep_for 的 await_suspend 调 register_timer
};

// ---------------------------------------------------------------------------
// out-of-line: final_awaiter::await_suspend (需 RtExecutor 完整)
//   标 done -> 通过 post_local 把自身递回 local_ready_(下 tick)。
//   run() 的 drain 在 executor 线程读到 done_ 后 destroy(绝不 resume)。
// ---------------------------------------------------------------------------
inline void RtTask::promise_type::final_awaiter::await_suspend(
        std::coroutine_handle<promise_type> h) noexcept {
    h.promise().done_ = true;
    // final_suspend 在 run() 的 resume 栈 = executor 线程,走 local_ready_(下 tick destroy)。
    h.promise().executor_->post_local(h);
}

// ---------------------------------------------------------------------------
// 析构: 前置条件 —— 所有会 post_ready 的外部生产者必须已 join。
// 正常关停请先调 shutdown(); 本析构的 destroy_all_tasks() 仅为未 shutdown 时的
// 兜底回收(强制销毁仍在链表中的 parked 帧, 非正常路径)。
// ---------------------------------------------------------------------------
inline RtExecutor::~RtExecutor() {
    stop_flag_.store(true, std::memory_order_release);
    destroy_all_tasks();  // 兜底: 销毁所有仍在链表中的 parked 帧(executor 线程语义)
}

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------
inline void RtExecutor::spawn(RtTask task, std::string_view name) {
    auto h = task.release();  // 接管所有权
    auto& p = h.promise();
    p.executor_ = this;
    p.name_ = std::string(name);
    active_count_.fetch_add(1, std::memory_order_relaxed);
    task_list_push(p);
    post_local(h);  // spawn 与 run 同线程(已文档化), 走 local_ready_
}

// ---------------------------------------------------------------------------
// run: 非阻塞 tick。tick 边界快照 = R1 的正解(杜绝 yield 在单 tick 内无限重入)。
//   1) process_timers: 到期 timer -> local_ready_(本 tick 处理)。
//   2) tick_batch_.clear() + swap 自 local_ready_: 处理中新产生的 yield/final
//      落进"新的" local_ready_(下 tick 才处理),从根上让 yield 推迟到下一 tick。
//   3) 抽干 ready_ext_ 进 tick_batch_: 外部快照, 此刻已排队的才本 tick 处理。
//   4) 遍历 tick_batch_: done -> destroy(executor 线程), 否则 resume。
//      resume 期间的 yield/final -> local_ready_(下 tick);
//      外部 post -> ready_ext_(下 tick)。
// ---------------------------------------------------------------------------
inline void RtExecutor::run() {
    process_timers();
    tick_batch_.clear();
    local_ready_.swap(tick_batch_);  // swap 后 local_ready_ 为空, tick_batch_ 持本 tick 内部重投递
    Handle h;
    while (ready_ext_.dequeue(h)) tick_batch_.push_back(h);  // 外部快照
    for (Handle x : tick_batch_) {
        auto hp = std::coroutine_handle<RtTask::promise_type>::from_address(x.address());
        if (hp.promise().done_) {
            task_list_unlink(hp.promise());
            hp.destroy();
            active_count_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            hp.resume();
        }
    }
}

// ---------------------------------------------------------------------------
// run_blocking
// ---------------------------------------------------------------------------
inline void RtExecutor::run_blocking() {
#ifdef __linux__
    if (config_.pin_cpu >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(config_.pin_cpu, &cpuset);
        ::sched_setaffinity(0, sizeof(cpuset), &cpuset);
    }
#endif
    while (!is_finished()) {
        run();
        if (!is_finished()) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.idle_sleep_us));
        }
    }
}

// ===========================================================================
// awaitable: 仅可在 RtTask 协程内 co_await(依赖 promise_type::rt_executor)
// ===========================================================================

// co_await rt::sleep_for(d): 注册定时器并挂起; 到期由 run() 推 ready 后在
// executor 线程 resume。绝不在 timer 路径 resume。
struct sleep_awaiter {
    std::chrono::steady_clock::time_point deadline;
    bool await_ready() noexcept {
        return deadline <= std::chrono::steady_clock::now();
    }
    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise> h) noexcept {
        h.promise().rt_executor()->register_timer(deadline, h);
    }
    void await_resume() noexcept {}
};

template<typename Rep, typename Period>
sleep_awaiter sleep_for(std::chrono::duration<Rep, Period> d) {
    return sleep_awaiter{std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(d)};
}

// co_await rt::yield(): 把自身推回 local_ready_, 让出到下一个 tick 再恢复。
// 走 post_local(非 ready_ext_), 故 yield 在单 tick 内不会重入 —— 周期执行体该有的行为。
struct yield_awaiter {
    bool await_ready() noexcept { return false; }
    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise> h) noexcept {
        h.promise().rt_executor()->post_local(h);
    }
    void await_resume() noexcept {}
};
inline yield_awaiter yield() noexcept { return {}; }

// co_await rt::stop_requested(): 同步查询停止标志(不挂起)。
// task 在周期边界查此返回值后 co_return, 实现协作式取消。
struct stop_check_awaiter {
    bool result_ = false;
    bool await_ready() noexcept { return false; }
    template<typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        result_ = h.promise().rt_executor()->stop_requested();
        return false;  // 不挂起, 立即同步恢复
    }
    bool await_resume() noexcept { return result_; }
};
inline stop_check_awaiter stop_requested() noexcept { return {}; }

} // namespace flowcoro::rt
