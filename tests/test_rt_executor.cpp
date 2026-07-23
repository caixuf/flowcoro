// flowcoro::rt 确定性实时执行模型测试
//
// 核心不变量(被本测试覆盖): 所有 resume 和 destroy 都发生在 run() 调用线程;
// 跨线程事件只能 post_ready(h), 绝不 inline h.resume()。
//
// 注: 任务协程一律写成自由函数(参数 by-value 拷入帧, 共享状态 by-reference 指向
// 调用者拥有的局部变量), 而非带捕获的 lambda —— 因为 lambda 闭包对象若是临时量
// 会在 spawn 表达式结束时销毁, 而协程帧持有指向闭包的 this, resume 时即成悬垂。

#include "flowcoro.hpp"
#include "test_framework.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>

using namespace flowcoro;
using namespace std::chrono_literals;

namespace {

// 辅助: 作为宿主驱动 executor 直到 is_finished() 或超时。
bool drive_until_finished(rt::RtExecutor& exec,
                         std::chrono::milliseconds timeout = 2s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    int guard = 0;
    while (!exec.is_finished()) {
        exec.run();
        if (exec.is_finished()) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (++guard > 100000) return false;
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return exec.is_finished();
}

// 测试用 park awaiter: 挂起协程, 把自身 handle 地址写到共享变量,
// 由"外部线程"读取后调 post_ready(h)。模拟 FlowEngine CompletionNotifier 场景。
struct park_awaiter {
    std::atomic<void*>* out;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        out->store(h.address(), std::memory_order_release);
    }
    void await_resume() noexcept {}
};

// --- 任务协程: 自由函数, 帧 hold 值副本 + 指向调用者局部的引用 ---
rt::RtTask basic_worker(std::atomic<bool>& ran) {
    ran.store(true, std::memory_order_release);
    co_return;
}

rt::RtTask sleep_worker(std::atomic<int>& phase) {
    phase.store(1, std::memory_order_release);
    co_await rt::sleep_for(30ms);
    phase.store(2, std::memory_order_release);
    co_return;
}

rt::RtTask stop_loop_worker(std::atomic<int>& iters) {
    while (!co_await rt::stop_requested()) {
        iters.fetch_add(1, std::memory_order_relaxed);
        co_await rt::sleep_for(2ms);
    }
    co_return;
}

rt::RtTask park_worker(std::atomic<void*>& parked, std::atomic<bool>& resumed) {
    co_await park_awaiter{&parked};  // 挂起, 等外部唤醒
    resumed.store(true, std::memory_order_release);
    co_return;
}

rt::RtTask throwing_worker() {
    throw std::runtime_error("boom");
    co_return;  // unreachable; 仅为维持协程形状
}

rt::RtTask other_worker(std::atomic<bool>& other_ran) {
    other_ran.store(true, std::memory_order_release);
    co_return;
}

rt::RtTask multi_worker(int dur_ms, std::atomic<int>& done_count) {
    co_await rt::sleep_for(std::chrono::milliseconds(dur_ms));
    done_count.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

rt::RtTask periodic_worker(std::atomic<int>& count) {
    while (!co_await rt::stop_requested()) {
        count.fetch_add(1, std::memory_order_relaxed);
        co_await rt::sleep_for(1ms);
    }
    co_return;
}

rt::RtTask noop_worker() {
    co_return;
}

} // namespace

// ---------------------------------------------------------------------------
// 1) 基本: spawn 一个一次性任务, run() 驱动它跑到 co_return, 帧在 executor 线程销毁。
// ---------------------------------------------------------------------------
TEST_CASE(rt_basic_spawn_run) {
    std::atomic<bool> ran{false};
    rt::RtExecutor exec;
    exec.spawn(basic_worker(ran), "basic");

    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_TRUE(ran.load());
    TEST_EXPECT_TRUE(exec.is_finished());
}

// ---------------------------------------------------------------------------
// 2) sleep_for: 任务挂起到 timer 堆, run() 每 tick 处理到期 timer -> 推 ready -> resume。
//    绝不在 timer 路径 resume。
// ---------------------------------------------------------------------------
TEST_CASE(rt_sleep_for) {
    std::atomic<int> phase{0};
    rt::RtExecutor exec;
    exec.spawn(sleep_worker(phase), "sleep");

    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_EQ(phase.load(), 2);
    TEST_EXPECT_TRUE(exec.is_finished());
}

// ---------------------------------------------------------------------------
// 3) stop_requested: 周期任务在周期边界查 stop, 协作式退出(两段式拆除)。
// ---------------------------------------------------------------------------
TEST_CASE(rt_stop_requested) {
    std::atomic<int> iters{0};
    rt::RtExecutor exec;
    exec.spawn(stop_loop_worker(iters), "stoploop");

    // 驱动若干 tick 让它跑几轮
    for (int i = 0; i < 20; ++i) {
        exec.run();
        std::this_thread::sleep_for(2ms);
    }
    const int before = iters.load();
    exec.request_stop();
    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(before > 0);
}

// ---------------------------------------------------------------------------
// 4) 核心不变量: 跨线程事件必须经 post_ready(h), 绝不 inline resume。
//    外部线程 post_ready, resume 只发生在 run() 的 executor 线程。
// ---------------------------------------------------------------------------
TEST_CASE(rt_post_ready_cross_thread) {
    std::atomic<void*> parked{nullptr};
    std::atomic<bool> resumed{false};
    rt::RtExecutor exec;
    exec.spawn(park_worker(parked, resumed), "parked");

    // 驱动直到任务 park
    int guard = 0;
    while (!parked.load(std::memory_order_acquire) && guard < 2000) {
        exec.run();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        ++guard;
    }
    TEST_EXPECT_TRUE(parked.load() != nullptr);

    // 另一线程: 取出 handle 调 post_ready(不 inline resume)
    std::thread poster([&]() {
        auto h = std::coroutine_handle<>::from_address(parked.load());
        exec.post_ready(h);
    });
    poster.join();

    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_TRUE(resumed.load());
    TEST_EXPECT_TRUE(exec.is_finished());
}

// ---------------------------------------------------------------------------
// 5) 未捕获异常: 记日志 + 标 dead, executor 继续跑其它任务。
// ---------------------------------------------------------------------------
TEST_CASE(rt_unhandled_exception) {
    std::atomic<bool> other_ran{false};
    rt::RtExecutor exec;
    exec.spawn(throwing_worker(), "throws");
    exec.spawn(other_worker(other_ran), "other");

    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_TRUE(other_ran.load());
    TEST_EXPECT_TRUE(exec.is_finished());
}

// ---------------------------------------------------------------------------
// 6) 多任务交错: 若干 sleep 任务并发, run() 公平抽干 ready。
// ---------------------------------------------------------------------------
TEST_CASE(rt_multiple_tasks) {
    constexpr int N = 5;
    std::atomic<int> done_count{0};
    rt::RtExecutor exec;
    for (int i = 0; i < N; ++i) {
        // dur_ms 按值拷入帧, 每个任务独立持有
        exec.spawn(multi_worker(10 + i * 5, done_count), "t");
    }
    TEST_EXPECT_TRUE(drive_until_finished(exec, 5s));
    TEST_EXPECT_EQ(done_count.load(), N);
    TEST_EXPECT_TRUE(exec.is_finished());
}

// ---------------------------------------------------------------------------
// 7) run_blocking 阻塞模式 + 跨线程 request_stop: 周期任务直到 stop 才结束。
// ---------------------------------------------------------------------------
TEST_CASE(rt_run_blocking_periodic) {
    std::atomic<int> count{0};
    rt::RtExecutor exec;
    exec.spawn(periodic_worker(count), "periodic");

    std::thread stopper([&]() {
        std::this_thread::sleep_for(50ms);
        exec.request_stop();
    });
    exec.run_blocking();  // 阻塞直到 is_finished
    stopper.join();

    TEST_EXPECT_TRUE(exec.is_finished());
    TEST_EXPECT_TRUE(count.load() > 0);
}

// ---------------------------------------------------------------------------
// 8) stop_token(): 外部(宿主)可持有令牌查询停止状态。
// ---------------------------------------------------------------------------
TEST_CASE(rt_stop_token_external) {
    rt::RtExecutor exec;
    auto tok = exec.stop_token();
    TEST_EXPECT_FALSE(tok.requested());
    exec.request_stop();
    TEST_EXPECT_TRUE(tok.requested());
    // 无任务: spawn 一个立即 co_return 的以让 active 归零
    exec.spawn(noop_worker(), "noop");
    TEST_EXPECT_TRUE(drive_until_finished(exec));
    TEST_EXPECT_TRUE(exec.is_finished());
}

int main() {
    TEST_SUITE("flowcoro::rt deterministic executor");
    flowcoro::test::TestRunner::print_summary();
    return flowcoro::test::TestRunner::all_passed() ? 0 : 1;
}
