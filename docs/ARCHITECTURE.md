# FlowCoro 架构设计

**基于无锁队列的高性能协程调度系统**

> 性能数据: 详细的性能指标请参考 [性能数据参考](PERFORMANCE_DATA.md)

## 核心设计理念

FlowCoro 采用**三层调度架构**，结合无锁队列和智能负载均衡，专门为高吞吐量批量任务处理优化：

- **无锁队列调度**: 基于lockfree::Queue的高性能任务分发
- **智能负载均衡**: 自适应调度器选择，最小化队列长度差异
- **Task同步启动**: 通过suspend_never实现任务创建时在调用者线程上同步执行，直到首个挂起点才进入调度系统

## 三层调度架构

```text
应用层协程 (Task<T>)
    ↓ suspend_never同步执行
协程遇到co_await挂起
    ↓ schedule_resume调度
协程管理器 (CoroutineManager) - 调度决策和负载均衡
    ↓ schedule_coroutine_enhanced
协程池 (CoroutinePool) - 多调度器并行处理
    ↓ 无锁队列分发
线程池 (ThreadPool) - 底层工作线程执行
```

### 第一层：协程管理器 (CoroutineManager)

**职责**: 协程生命周期管理、智能负载均衡、定时器处理

```cpp
class CoroutineManager {
public:
    // 核心调度方法 - 使用智能负载均衡
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
- **智能调度**: 负载均衡选择最优调度器
- **批量处理**: 减少锁竞争，提升吞吐量
- **生命周期管理**: 安全的协程创建和销毁
- **后台线程**（近期实现）: 专用定时器线程驱动 timer（`get()` 不再需要 `drive()` 全局 manager 推进 timer，见 `coroutine_manager.h` FC-5）；后台回收线程（reaper）周期 drain 延迟销毁队列（FC-2）

### 第二层：协程池 (CoroutinePool)

**职责**: 多调度器并行处理、无锁队列管理、智能负载均衡

```cpp
class CoroutinePool {
private:
    // 调度器数量（当前固定为 1，性能最优配置，见 src/coroutine_pool.cpp NUM_SCHEDULERS）
    const size_t NUM_SCHEDULERS;
    
    // 独立协程调度器，每个管理一个无锁队列
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 智能负载均衡器
    SmartLoadBalancer load_balancer_;
    
public:
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 选择负载最轻的调度器
        size_t scheduler_index = load_balancer_.select_scheduler();
        
        // 投递到对应的无锁队列
        schedulers_[scheduler_index]->schedule_coroutine(handle);
        
        // 更新负载统计
        load_balancer_.increment_load(scheduler_index);
    }
};
```

**特点**:
- **单调度器（当前）**: 调度器数量固定为 1（`NUM_SCHEDULERS=1`，性能最优配置）；多调度器与负载均衡框架保留，便于后续扩展
- **无锁队列**: 调度器使用独立的 lockfree::Queue
- **批量处理**: worker 循环每次批量取出最多 256 个协程执行，降低调度开销
- **智能负载均衡**: 自动选择负载最轻的调度器，避免热点

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
                std::this_thread::yield();
            }
        }
    }
};
```

**特点**:
- **无锁队列**: 使用lockfree::Queue避免锁竞争
- **自适应线程数**: 根据CPU核心数调整工作线程数量
- **CPU友好**: 空闲时使用yield而非spin-wait

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
// 3. await_suspend调用schedule_resume -> 投递到协程管理器
// 4. 负载均衡选择调度器 -> 进入无锁队列 -> 工作线程获取 -> 协程恢复执行
```

### 2. 并发机制

```text
Task task1 = compute(10);  // 同步执行直到挂起点，然后进入调度
Task task2 = compute(20);  // 同步执行直到挂起点，然后进入调度
Task task3 = compute(30);  // 同步执行直到挂起点，然后进入调度

// 此时三个任务可能已在不同调度器的队列中等待或正在执行
auto result1 = co_await task1;  // 等待第一个任务完成
auto result2 = co_await task2;  // 等待第二个任务完成
auto result3 = co_await task3;  // 等待第三个任务完成
```

### 3. 智能负载均衡

```cpp
class SmartLoadBalancer {
    std::atomic<size_t> current_scheduler_{0};
    std::vector<std::atomic<size_t>> scheduler_loads_;
    
public:
    size_t select_scheduler() {
        // 轮询策略 + 负载感知
        size_t min_load = scheduler_loads_[0].load();
        size_t best_scheduler = 0;
        
        for (size_t i = 1; i < scheduler_loads_.size(); ++i) {
            size_t load = scheduler_loads_[i].load();
            if (load < min_load) {
                min_load = load;
                best_scheduler = i;
            }
        }
        
        return best_scheduler;
    }
};
```

## 关键特性

### 无锁架构
- 所有队列操作都是无锁的
- 避免了传统锁的性能开销
- 支持高并发场景下的任务调度

### suspend_never机制
- Task创建时在调用者线程上同步开始执行
- 执行直到遇到第一个co_await挂起点
- 挂起后才进入调度器队列进行异步调度

### 智能负载均衡
- 自动选择最优调度器
- 避免热点和负载不均
- 支持动态负载调整

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

> API 细节见 [API 参考 §9](API_REFERENCE.md#9-确定性实时执行-rtexecutor)；
> 自动驾驶场景示例见 [examples/autonomous_driving/ad_pipeline_demo.cpp](../examples/autonomous_driving/ad_pipeline_demo.cpp)。

### 核心正确性契约

```
所有 resume 和 destroy 都发生在 run() 的调用线程（executor 线程）上。
事件在别的线程发生时，只能 post_ready(h) 把 handle 递回来，绝不 inline h.resume()。
```

这是确定性（单线程）的根基：执行器线程是唯一修改协程帧状态的地方，
跨线程只通过无锁队列/原子传递句柄，从根上避免数据竞争。

### 事件流（双队列 + tick 快照）

- **内部重投递**（spawn/yield/final/timer）：走 executor 线程私有的 `local_ready_` vector，
  稳态零分配。
- **跨线程事件**：走 `ready_ext_`（MPSC 无锁队列，`post_ready`）。

`run()` 是非阻塞 tick，每次调用：

1. `process_timers`：到期 timer 的 handle → `local_ready_`（绝不在 timer 路径 resume）；
   stop 已请求时取消全部剩余 timer。
2. tick 边界快照：`tick_batch_.clear()` + swap 自 `local_ready_` + 抽干 `ready_ext_`。
   处理中新产生的 yield/final 落进"新的" `local_ready_`（下一 tick 才处理）——
   从根上杜绝 yield 在单 tick 内无限重入。
3. 遍历 tick_batch：done 帧 → destroy（executor 线程），其余 → resume。

不阻塞、不 notify、不 syscall。

### 两段式拆除

```
request_stop() -> 置标志
  -> 下一次 run() 取消 timer（推 local_ready_）
  -> task 在周期边界查 stop 后 co_return 到 final_suspend（park，标 done，推 local_ready_）
  -> run() 的 drain 把 done 帧在 executor 线程 destroy
  -> active 空 => is_finished()
```

优雅关停请用 `request_stop()` + 反复 `run()`（或 `shutdown()`）；析构仅为未关停时的兜底回收。

### 关键 API

| API | 说明 |
|-----|------|
| `RtExecutor::Config{.pin_cpu, .idle_sleep_us}` | pin_cpu 绑定宿主线到指定核（run_blocking，Linux） |
| `spawn(RtTask, name)` | 注册任务，惰性启动（initial_suspend = suspend_always），须与 run 同线程 |
| `run()` | 非阻塞 tick，宿主每周期调一次 |
| `run_blocking()` | 阻塞便捷模式：loop run() + 空闲睡眠直到 is_finished() |
| `request_stop()` / `stop_token()` | 协作式停止（周期边界检查，不抢占） |
| `post_ready(h)` | 跨线程事件唯一合法出口（MPSC 无锁入队，不 inline resume） |
| `rt::sleep_for(d)` / `rt::yield()` / `rt::stop_requested()` | 仅在 RtTask 协程内 co_await 的 awaitable |

### 与三层调度的定位差异

| 维度 | 三层调度（Task/CoroutinePool） | 实时层（flowcoro::rt） |
|------|-------------------------------|------------------------|
| 目标 | 高吞吐批处理、负载均衡 | 延迟确定、单线程亲和 |
| 执行模型 | 多调度器 + 线程池并行 | 单线程周期 tick |
| 线程 | 多工作线程 | 单 host 线程（可 CPU 绑定） |
| resume 发生地 | 任意工作线程 | 固定 executor 线程 |
| 典型场景 | Web/网关/批处理 | 机器人/自动驾驶/嵌入式控制 |

## 性能特征

详细性能数据请参考 [性能数据参考](PERFORMANCE_DATA.md)。

核心特征：
- **高吞吐量**: 专为批量任务处理优化
- **低延迟**: 无锁架构减少调度开销
- **高并发**: 多调度器支持大量并发任务
- **智能调度**: 自适应负载均衡
