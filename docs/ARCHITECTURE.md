# FlowCoro 架构设计

**基于无锁队列的高性能协程调度系统**

> 性能数据: 详细的性能指标请参考 [性能数据参考](PERFORMANCE_DATA.md)

## 核心设计理念

FlowCoro 采用**三层调度架构**，结合无锁队列，专门为高吞吐量批量任务处理优化：

- **无锁队列调度**: 基于 `lockfree::Queue` 的任务分发
- **单协程调度器（默认）**: `NUM_SCHEDULERS=1`。并行来自后台 `ThreadPool`，不是多个 CoroutineScheduler
- **Task同步启动**: 通过 `suspend_never` 在调用者线程上同步执行，直到首个挂起点才进入调度系统

## 三层调度架构

```text
应用层协程 (Task<T>)
    ↓ suspend_never同步执行
协程遇到co_await挂起
    ↓ schedule_resume调度
协程管理器 (CoroutineManager) - 生命周期 / 定时器 / 投递到协程池
    ↓ schedule_coroutine_enhanced
协程池 (CoroutinePool) - 默认 1 个 CoroutineScheduler
    ↓ 无锁队列
调度器线程 resume 协程；CPU/阻塞工作另走 ThreadPool
```

### 第一层：协程管理器 (CoroutineManager)

**职责**: 协程生命周期管理、定时器处理、把恢复投递到 CoroutinePool

```cpp
class CoroutineManager {
public:
    // 核心调度方法 - 投递到协程池（默认单个调度器）
    void schedule_resume(std::coroutine_handle<> handle) {
        if (!handle || handle.done()) return;
        
        // 检查协程是否已销毁
        if (handle.promise().is_destroyed()) return;
        
        // 投递到协程池进行并行处理
        schedule_coroutine_enhanced(handle);
    }
    
    // 驱动调度循环 - 批量处理优化
    void drive() {
        drive_coroutine_pool();        // 驱动协程池
        process_timer_queue();         // 批量处理定时器(32个/批)
        process_ready_queue();         // 批量处理就绪队列(64个/批)
        process_pending_tasks();       // 批量销毁协程(64个/批)
    }
```

**特点**:
- **单调度器投递**: 默认不选择「最优调度器」——只有一个
- **批量处理**: 减少锁竞争，提升吞吐量
- **生命周期管理**: 安全的协程创建和销毁
- **后台线程**（近期实现）: 专用定时器线程驱动 timer（`get()` 不再需要 `drive()` 全局 manager 推进 timer，见 `coroutine_manager.h` FC-5）；后台回收线程（reaper）周期 drain 延迟销毁队列（FC-2）

### 第二层：协程池 (CoroutinePool)

**职责**: 默认单个协程调度器、无锁队列、把 CPU/阻塞任务交给 ThreadPool

```cpp
class CoroutinePool {
private:
    // 默认 1。>1 仅 cmake -DFLOWCORO_NUM_SCHEDULERS=N 实验性 opt-in
    const size_t NUM_SCHEDULERS;

    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;

public:
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 默认：直达 schedulers_[0]。没有「选最轻负载」这一步。
        schedulers_[0]->schedule_coroutine(handle);
    }
};
```

**特点**:
- **单调度器（默认 / 受支持的故事）**: `FLOWCORO_NUM_SCHEDULERS=1`。云上微型基准里 4 调度器并未打赢 1 调度器；不要把多调度器写成当前架构。
- **无锁队列**: 调度器使用独立的 `lockfree::Queue`
- **批量处理**: worker 循环每次最多取出 256 个协程
- **空闲等待**: 短自旋 / yield 后 `IdlePark`（Linux futex，否则 cv）。enqueue 在有 waiter 时 wake。不再使用不可打断的 `sleep_for`
- **实验性多调度器**: `-DFLOWCORO_NUM_SCHEDULERS=N`（N>1）才走 `SmartLoadBalancer`。`try_steal_work` 已删除（从未接到 idle 循环）。这不是默认功能

### 第三层：线程池 (ThreadPool)

**职责**: 底层工作线程管理、无锁任务执行

```cpp
class ThreadPool {
private:
    // 无锁任务队列
    lockfree::Queue<std::function<void()>> task_queue_;
    
    // 工作线程数组
    std::vector<std::thread> workers_;
    
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }
    
    void enqueue_void(std::function<void()> task) {
        if (!stop_.load(std::memory_order_acquire)) {
            task_queue_.enqueue(std::move(task));
        }
    }
    
private:
    void worker_loop() {
        std::function<void()> task;
        while (!stop_.load(std::memory_order_acquire)) {
            if (task_queue_.dequeue(task)) {
                task();  // 执行协程恢复
            } else {
                idle_.wait([this] {
                    return stop_.load() || !task_queue_.empty();
                });
            }
        }
    }
};
```

**特点**:
- **无锁队列**: 使用lockfree::Queue避免锁竞争
- **自适应线程数**: 根据CPU核心数调整工作线程数量
- **CPU友好**: 空闲时短自旋后 park（可被 enqueue 唤醒），不是裸 yield / 不可打断 sleep

## 任务执行流程

### 1. Task创建和同步执行

```cpp
Task<int> compute(int x) {
    // Task创建时在调用者线程上同步开始执行
    co_await sleep_for(std::chrono::milliseconds(50));  // 首个挂起点，进入调度器
    co_return x * x;
}

// 任务创建时的执行流程：
// 1. Task构造函数调用 -> initial_suspend() -> suspend_never
// 2. 协程体在调用者线程上同步执行 -> 遇到co_await -> 挂起
// 3. 挂起后才进入调度器队列进行异步调度
```

### 2. 并发机制

```text
Task task1 = compute(10);  // 同步执行直到挂起点，然后进入调度
Task task2 = compute(20);
Task task3 = compute(30);

auto result1 = co_await task1;
auto result2 = co_await task2;
auto result3 = co_await task3;
```

并行 resume 默认发生在**同一个** CoroutineScheduler 线程上；真正的多线程并行来自 `ThreadPool`（`schedule_task` / `GlobalThreadPool`）以及应用自己的线程。`flowcoro::rt` 是另一套单线程实时模型。

### 3. 负载均衡（不是默认路径）

`SmartLoadBalancer` 只在 `FLOWCORO_NUM_SCHEDULERS>1` 时参与入队。默认构建不会「选择最优调度器」。不要把它列成核心特性。

## 关键特性

### 无锁架构
- 所有队列操作都是无锁的
- 避免了传统锁的性能开销
- 支持高并发场景下的任务调度

### suspend_never机制
- Task创建时在调用者线程上同步开始执行
- 执行直到遇到第一个co_await挂起点
- 挂起后才进入调度器队列进行异步调度

### 空闲 park
- ThreadPool / CoroutineScheduler 空闲：短自旋后 park，enqueue 唤醒
- 默认不把负载均衡当成调度策略

### 内存池优化
- Redis/Nginx启发的内存分配策略
- 减少malloc/free调用
- 提升内存分配性能

## 适用场景

**完美适配**:
- **批量并发任务处理**: Web API 服务、数据处理管道
- **请求-响应模式**: HTTP 服务器、RPC 服务、代理网关
- **独立任务并发**: 爬虫系统、测试工具、文件处理
- **高性能计算**: 大规模并行计算任务
- **生产者-消费者模式**: 通过Channel实现协程间通信

**不适合**:
- 需要精确协程执行顺序的场景
- 单一长时间运行的协程
- 需要手动管理协程生命周期的场景

## 实时执行模型（flowcoro::rt）

三层调度架构面向**高吞吐批处理**；而 `flowcoro::rt` 的 `RtExecutor` 面向**延迟敏感的控制回路**
（机器人/自动驾驶/嵌入式），两者是互补的两套调度模型，互不干扰。

> API 细节见 [API 参考 §9](API_REFERENCE.md#9-确定性实时执行-rtexecutor)。
> 实时契约示例：[`rt_control_loop_demo.cpp`](../examples/autonomous_driving/rt_control_loop_demo.cpp)（无 DDS）。
> Task+DDS 中间件示例：[`ad_pipeline_demo.cpp`](../examples/autonomous_driving/ad_pipeline_demo.cpp)（不在 RtExecutor 上跑）。

### 核心正确性契约

```
所有 resume 和 destroy 都发生在 run() 的调用线程（executor 线程）上。
事件在别的线程发生时，只能 post_ready(h) 把 handle 递回来，绝不 inline h.resume()。
```

这是确定性（单线程）的根基：执行器线程是唯一修改协程帧状态的地方，
跨线程只通过无锁队列/原子传递句柄，从根上避免数据竞争。

### 事件流（双队列 + tick 快照）

- **内部重投递**（spawn/yield/final/timer）：走 executor 线程私有的 `local_ready_` vector，
  构造时 reserve，稳态不再堆分配。
- **跨线程事件**：走 `ready_ext_`（`lockfree::Queue`，`post_ready`）。**每次入队分配节点**；
  dequeue 走 hazard pointer，retire 阈值上有自旋锁。不要把这条路径说成「稳态零分配 / 零 syscall」。

`run()` 是非阻塞 tick：

1. `process_timers`：到期 timer 的 handle → `local_ready_`（绝不在 timer 路径 resume）；
   stop 已请求时取消全部剩余 timer。
2. tick 边界快照：`tick_batch_.clear()` + swap 自 `local_ready_` + 抽干 `ready_ext_`。
   处理中新产生的 yield/final 落进「新的」`local_ready_`（下一 tick 才处理）。
3. 遍历 tick_batch：done 帧 → destroy（executor 线程），其余 → resume。

`run()` 本身不阻塞、不 notify。本地热路径预热后无 syscall；`run_blocking()` 在确认空闲后
`sleep_until`（syscall），有 pending yield 时**不再盲睡**。

### 两段式拆除

```
request_stop() -> 置标志
  -> 下一次 run() 取消 timer（推 local_ready_）
  -> task 在周期边界查 stop 后 co_return 到 final_suspend（park，标 done，推 local_ready_）
  -> 再一次 run() 的 drain 把 done 帧在 executor 线程 destroy
  -> active 空 => is_finished()
```

优雅关停请用 `request_stop()` + 反复 `run()`（或 `shutdown()`）；析构仅为未关停时的兜底回收。
`shutdown()` 前必须 join 所有 `post_ready` 生产者。

### 关键 API

| API | 说明 |
|-----|------|
| `Config{.pin_cpu, .idle_sleep_us}` | `pin_cpu` 在第一次 `run()`/`run_blocking()`/`apply_affinity()` 绑当前线程（Linux）；`idle_sleep_us` 只约束 `run_blocking` 空闲睡眠，`0` = 忙等 |
| `spawn(RtTask, name)` | 注册任务，惰性启动，须与 run 同线程 |
| `run()` | 非阻塞 tick；控制回路宿主每周期调一次 |
| `run_blocking()` | 便捷 loop；有本地待处理时不 sleep |
| `next_timer_deadline()` / `has_local_work()` | 给宿主写自己的等待（timerfd / `sleep_until`） |
| `request_stop()` / `stop_token()` | 协作式停止（周期边界检查，不抢占） |
| `post_ready(h)` | 跨线程唯一合法出口；禁止重复投递同一 parked handle |
| `rt::sleep_until` / `sleep_for` / `yield` / `stop_requested` | 仅在 RtTask 内 co_await |

### 延迟灯笼

`tests/test_rt_latency.cpp` 跑 10ms 周期的 `sleep_until` 回路，报告 p50/p99/max 间隔与 tardiness。
CI 用宽松墙钟预算（p99 late ≤ 200ms）；本机 `FLOWCORO_RT_SLO_STRICT=1` 收到 3ms/15ms。
数字是这次跑的测量，不是硬实时认证。

### 与三层调度的定位差异

| 维度 | 三层调度（Task/CoroutinePool） | 实时层（flowcoro::rt） |
|------|-------------------------------|------------------------|
| 目标 | 高吞吐批处理 | 延迟确定、单线程亲和 |
| 执行模型 | 单 CoroutineScheduler + ThreadPool | 单线程周期 tick |
| 线程 | 1 个调度器线程 + N 个线程池 worker | 单 host 线程（可 CPU 绑定） |
| resume 发生地 | 默认固定在调度器线程 | 固定 executor 线程 |
| 典型场景 | Web/网关/批处理 | 机器人/自动驾驶/嵌入式控制 |

## 性能特征

详细性能数据请参考 [性能数据参考](PERFORMANCE_DATA.md)。

核心特征：
- **高吞吐量**: 专为批量任务处理优化
- **低延迟**: 无锁架构减少调度开销
- **并行**: 后台 ThreadPool 跑 CPU/阻塞工作；协程 resume 默认单调度器
