# FlowCoro 架构设计

**基于无锁队列的高性能协程调度系统**

> **📖 完整调度设计文档**: 详细的调度系统设计和实现细节请参考 [调度系统设计详解](SCHEDULING_DESIGN.md)
> 
> 性能数据: 详细的性能指标请参考 [性能数据参考](PERFORMANCE_DATA.md)

## 核心设计理念

FlowCoro 采用**三层调度架构**，结合无锁队列和智能负载均衡，专门为高吞吐量批量任务处理优化：

- **无锁队列调度**: 基于lockfree::Queue的高性能任务分发
- **智能负载均衡**: 自适应调度器选择，最小化队列长度差异
- **Task立即执行**: 通过suspend_never实现任务创建时立即投递到调度系统

## 三层调度架构

```text
应用层协程 (Task<T>)
    ↓ suspend_never立即投递
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
        process_pending_tasks();       // 批量销毁协程(32个/批)
    }
};
```

**特点**:
- **智能调度**: 负载均衡选择最优调度器
- **批量处理**: 减少锁竞争，提升吞吐量
- **生命周期管理**: 安全的协程创建和销毁

### 第二层：协程池 (CoroutinePool)

**职责**: 多调度器并行处理、无锁队列管理、智能负载均衡

```cpp
class CoroutinePool {
private:
    // 根据CPU核心数确定调度器数量 (通常4-16个)
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
- **多调度器**: 4-16个独立调度器并行工作
- **无锁队列**: 每个调度器使用独立的lockfree::Queue
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

### 1. Task创建和立即执行

```cpp
Task<int> compute(int x) {
    // Task创建时通过suspend_never立即投递到调度系统
    co_await sleep_for(std::chrono::milliseconds(50));
    co_return x * x;
}

// 任务创建时的执行流程：
// 1. Task构造函数调用 -> initial_suspend() -> suspend_never
// 2. 协程体开始执行 -> 遇到co_await -> suspend_always
// 3. 投递到协程管理器 -> 负载均衡选择调度器
// 4. 进入无锁队列 -> 工作线程获取 -> 协程恢复执行
```

### 2. 并发机制

```text
Task task1 = compute(10);  // 立即开始并发执行
Task task2 = compute(20);  // 立即开始并发执行
Task task3 = compute(30);  // 立即开始并发执行

// 此时三个任务已经在不同调度器上并发运行
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
- Task创建时立即开始执行
- 无需等待调度即可开始工作
- 最大化并发度

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

## 性能特征

详细性能数据请参考 [性能数据参考](PERFORMANCE_DATA.md)。

核心特征：
- **高吞吐量**: 专为批量任务处理优化
- **低延迟**: 无锁架构减少调度开销
- **高并发**: 多调度器支持大量并发任务
- **智能调度**: 自适应负载均衡
