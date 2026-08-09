# FlowCoro API 参考文档

## 概述

FlowCoro 是一个基于 C++20 协程的高性能异步编程库，采用**同步启动+挂起调度**的独特并发模型。

### 核心特性

- **同步启动**: 协程创建时在调用者线程上同步执行直到首个挂起点
- **无锁架构**: 多层无锁队列实现高性能协程调度
- **智能负载均衡**: CPU亲和性绑定 + 动态负载感知
- **内存池优化**: Redis/Nginx风格的内存管理
- **批量处理**: 256协程批次处理，减少调度开销

## 目录

### 核心概念

- [并发机制深度解析](#并发机制深度解析) - Task创建时同步执行，co_await挂起后进入调度
- [架构限制](#架构限制) - 不支持的使用模式
- [适用场景](#适用场景) - 推荐和不推荐的用法

### API参考

- [1. Task](#1-task) - 协程任务接口
- [2. sync_wait()](#2-sync_wait) - 同步等待
- [3. when_all()](#3-when_all) - 批量等待语法糖
- [4. sleep_for()](#4-sleep_for) - 协程友好的延时
- [5. Channel](#5-channel) - 协程间通信通道
- [6. 线程池](#6-线程池) - 后台任务处理
- [7. 内存管理](#7-内存管理) - 内存池和对象池
- [8. 取消 (CancellationToken)](#8-取消-cancellationtoken) - 结构化取消
- [9. 确定性实时执行 (RtExecutor)](#9-确定性实时执行-rtexecutor) - 单线程实时执行模型

---

## 并发机制深度解析

### FlowCoro的独特并发模型

FlowCoro采用**同步启动+挂起调度**的并发模型，与传统Go/Rust协程有本质区别：

#### 1. 协程同步启动然后挂起 (suspend_never)

```cpp
// FlowCoro: 协程创建时在调用者线程上同步开始执行
Task<int> async_compute(int value) {
    // 这个协程在创建时在调用者线程上同步开始执行
    // 不需要立即投递到调度器
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return value * 2;
}

Task<void> concurrent_processing() {
    // 三个协程依次同步开始执行，遇到挂起点后进入调度系统
    auto task1 = async_compute(1);  // 同步执行直到挂起，然后调度
    auto task2 = async_compute(2);  // 同步执行直到挂起，然后调度  
    auto task3 = async_compute(3);  // 同步执行直到挂起，然后调度
    
    // co_await等待结果，协程可能已经完成或在调度器中
    auto r1 = co_await task1;  // 可能立即返回结果或等待
    auto r2 = co_await task2;  // 可能立即返回结果或等待
    auto r3 = co_await task3;  // 可能立即返回结果或等待
    
    std::cout << "Results: " << r1 << ", " << r2 << ", " << r3 << std::endl;
}
```

#### 2. 多层调度器架构

```cpp
// 协程调度器内部实现（简化版）
class CoroutineScheduler {
    // 无锁队列：高性能协程分发
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    void worker_loop() {
        // CPU亲和性：绑定特定CPU核心
        set_cpu_affinity();
        
        // 批量处理：256个协程一批
        const size_t BATCH_SIZE = 256;
        std::vector<std::coroutine_handle<>> batch;
        
        while (!stop_flag_) {
            // 批量提取协程
            while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
            }
            
            // 批量执行（减少调度开销）
            for (auto handle : batch) {
                handle.resume();
            }
            
            // 自适应等待策略
            adaptive_wait_strategy();
        }
    }
};
```

#### 3. 智能负载均衡

```cpp
class SmartLoadBalancer {
    // 实时负载跟踪（无锁）
    std::array<std::atomic<size_t>, MAX_SCHEDULERS> queue_loads_;
    
    size_t select_scheduler() {
        // 混合策略：性能 + 公平性
        size_t quick_choice = round_robin_counter_.fetch_add(1) % scheduler_count_;
        
        // 每16次执行负载检查（避免过度优化）
        if ((quick_choice & 0xF) == 0) {
            return find_least_loaded_scheduler();
        }
        
        return quick_choice;
    }
};
```

### 并发性能特征

#### 高吞吐量场景

```cpp
Task<void> high_throughput_processing() {
    // 批量创建10000个任务
    std::vector<Task<int>> tasks;
    tasks.reserve(10000);
    
    // 创建10000个协程（每个同步执行直到首个挂起点）
    for (int i = 0; i < 10000; ++i) {
        tasks.emplace_back(async_process_data(i));
    }
    
    // 批量等待结果
    std::vector<int> results;
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
    
    co_return;
}
```

#### 内存池优化

```cpp
// 基于Redis/Nginx设计的内存池
class SimpleMemoryPool {
    // 多尺寸类别支持
    static constexpr size_t SIZE_CLASSES[] = {32, 64, 128, 256, 512, 1024};
    
    // 线程本地自由列表（无锁）
    struct FreeList {
        std::atomic<MemoryBlock*> head{nullptr};
        std::atomic<size_t> count{0};
    };
    
    void* pool_malloc(size_t size) {
        // 快速路径：从缓存获取
        if (auto* block = pop_from_freelist(size)) {
            return block;
        }
        // 慢速路径：系统分配
        return system_malloc(size);
    }
};
```

### 与其他框架的区别

#### vs Go Goroutines

```cpp
// Go: 需要调度器唤醒
go func() {
    time.Sleep(100 * time.Millisecond)  // 让出CPU，等待调度
    return value * 2
}()

// FlowCoro: 同步启动，挂起后调度
auto task = async_compute(value);  // 同步开始执行直到挂起点
```

#### vs Rust async/await

```rust
// Rust: 需要执行器轮询
let future = async {
    tokio::time::sleep(Duration::from_millis(100)).await;
    value * 2
};

// FlowCoro: 协程创建时同步开始执行
auto task = async_compute(value);  // 同步执行直到挂起
```

### 性能优化技术

#### 1. CPU亲和性优化

```cpp
void CoroutineScheduler::set_cpu_affinity() {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // 绑定到特定CPU核心，避免线程迁移
    CPU_SET(scheduler_id_ % std::thread::hardware_concurrency(), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}
```

#### 2. 内存预取优化

```cpp
// 预取下一个协程的内存，减少缓存未命中
while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
    batch.push_back(handle);
    
    if (batch.size() < BATCH_SIZE - 1) {
        __builtin_prefetch(handle.address(), 0, 3);  // 预取指令
    }
}
```

#### 3. 自适应等待策略

```cpp
void adaptive_wait_strategy() {
    if (empty_iterations < 64) {
        // Level 1: 纯自旋（最快响应）
        for (int i = 0; i < 64; ++i) {
            __builtin_ia32_pause();  // x86 pause指令
        }
    } else if (empty_iterations < 256) {
        // Level 2: 让出CPU时间片
        std::this_thread::yield();
    } else if (empty_iterations < 1024) {
        // Level 3: 短暂休眠
        std::this_thread::sleep_for(wait_duration);
    } else {
        // Level 4: 条件变量等待
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, max_wait);
    }
}
```

---

## 架构限制

FlowCoro 设计为**单向数据流协程**，不支持复杂的协程间通信模式。

### 不支持的模式

```cpp
// 错误：协程间直接通信
Task<void> wrong_pattern() {
    auto task1 = producer_coroutine();
    auto task2 = consumer_coroutine(task1);  // 依赖关系
    co_await task2;
}

// 错误：协程链式调用
Task<void> chained_wrong() {
    auto result1 = co_await step1();
    auto result2 = co_await step2(result1);
    auto result3 = co_await step3(result2);
}
```

### 推荐的模式

**批量任务处理:**

```cpp
// 正确：批量处理模式
Task<void> batch_requests() {
    std::vector<Task<Response>> tasks;
    
    // 批量创建任务（同步执行直到挂起点）
    for (const auto& request : requests) {
        tasks.emplace_back(process_request(request));
    }
    
    // 批量等待结果
    std::vector<Response> responses;
    for (auto& task : tasks) {
        responses.push_back(co_await task);
    }
    
    co_return;
}
```

### 强烈推荐

- **Web服务器**: 大量独立HTTP请求处理
- **数据处理**: 批量数据转换、计算
- **I/O密集型**: 并发文件读写、网络请求
- **批量数据处理**: 批量查询、文件处理
- **压力测试工具**: 大量并发请求
- **爬虫系统**: 并发网页抓取

### 不适合

- **实时系统**: 需要协程间持续通信
- **流处理**: 需要协程链式协作
- **事件驱动**: 需要协程间消息传递

---

## 1. Task

FlowCoro 的核心协程任务类，封装协程的生命周期管理。

### Task基本用法

```cpp
#include "flowcoro.hpp"
using namespace flowcoro;

// 定义协程函数
Task<int> compute_value(int x) {
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return x * x;
}

// 使用协程
Task<void> example() {
    auto task = compute_value(5);  // 创建时立即开始执行
    int result = co_await task;    // 等待结果
    std::cout << "Result: " << result << std::endl;
}
```

### 主要方法

#### co_await 操作符

等待协程完成并获取结果：

```cpp
Task<int> async_operation() {
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return 42;
}

Task<void> caller() {
    int result = co_await async_operation();  // 阻塞直到完成
    std::cout << "Got: " << result << std::endl;
}
```

#### 立即获取 (非阻塞检查)

```cpp
Task<void> non_blocking_check() {
    auto task = async_operation();
    
    // 检查是否已完成（非阻塞）
    if (task.is_ready()) {
        int result = co_await task;  // 立即返回
        std::cout << "Already done: " << result << std::endl;
    } else {
        std::cout << "Still running..." << std::endl;
        int result = co_await task;  // 等待完成
    }
}
```

### 生命周期

1. **创建**: `auto task = func()` - 协程同步开始执行直到首个挂起点
2. **挂起**: 遇到co_await时进入调度器队列
3. **执行**: 在协程池的工作线程中恢复执行
4. **等待**: `co_await task` - 获取结果（可能立即返回）
5. **销毁**: 超出作用域自动清理

### 状态查询与结果获取 (Promise 风格)

Task 提供一组查询/获取方法（对应 JavaScript Promise 语义，见 `task.h`）：

```cpp
T get(std::chrono::milliseconds timeout = 5s);  // 阻塞等待结果；超时抛 TaskTimeoutException
T get_result();                                 // 同 get()（SafeTask 兼容；void 特化）
std::optional<T> try_get() noexcept;            // 非阻塞获取：未完成/出错返回 nullopt

bool is_ready() const noexcept;                 // 是否已完成（等价 await_ready）
bool is_pending() const noexcept;               // 未完成且未取消
bool is_settled() const noexcept;               // 已结束（完成或取消）
bool is_fulfilled() const noexcept;             // 正常完成且无错误
bool is_rejected() const noexcept;              // 已取消或有错误
bool is_active() const noexcept;                // 句柄有效、未完成且未取消
bool is_cancelled() const noexcept;             // 是否已请求取消

void cancel();                                  // 请求取消（协作式）
void detach() noexcept;                         // 放弃帧所有权，交 CoroutineManager 回收
void attach_cancellation_token(const CancellationToken& t); // token 取消时本任务自动取消
```

**说明**: `cancel()`、`attach_cancellation_token()` 均为协作式取消——置取消标志并通知等待者，协程仍需在挂起点自行响应结束。`get()` 在协程未完成时阻塞等待，若在 `timeout` 内未完成则抛出 `TaskTimeoutException`（而非静默返回默认值）。

---

## 2. sync_wait

将协程转换为同步阻塞调用的工具函数。

### sync_wait函数签名

```cpp
template<typename T>
T sync_wait(Task<T>&& task);

void sync_wait(Task<void>&& task);

template<typename Func>
auto sync_wait(Func&& func);   // 调用函数并同步等待其返回的 Task
```

### sync_wait基本用法

```cpp
// 在非协程上下文中使用
int main() {
    auto task = compute_value(10);
    
    // 同步等待结果
    int result = sync_wait(task);
    std::cout << "Final result: " << result << std::endl;
    
    return 0;
}
```

### 注意事项

- `sync_wait` 是**同步阻塞**函数，会暂停当前线程执行
- 只能在**非协程**函数中使用
- 在协程内部应使用 `co_await` 而不是 `sync_wait`

---

## 3. when_all

批量等待多个协程完成的语法糖函数。

### when_all函数签名

```cpp
template<typename... Tasks>
auto when_all(Tasks&&... tasks);
```

### when_all基本用法

```cpp
Task<void> batch_processing() {
    // 创建多个并发任务
    auto task1 = process_data(1);
    auto task2 = process_data(2);
    auto task3 = process_data(3);
    
    // 批量等待所有任务完成
    auto [result1, result2, result3] = co_await when_all(task1, task2, task3);
    
    std::cout << "All results: " << result1 << ", " << result2 << ", " << result3 << std::endl;
}
```

### 动态数量任务

```cpp
Task<void> dynamic_batch() {
    std::vector<Task<int>> tasks;
    
    // 动态创建任务
    for (int i = 0; i < 10; ++i) {
        tasks.emplace_back(compute_value(i));
    }
    
    // 等待所有任务（手动循环）
    std::vector<int> results;
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
}
```

### 适用场景

- **固定数量任务**: 2-10个已知的协程任务
- **需要所有结果**: 批量API调用、并行计算

### 大量任务的替代方案

对于大量任务（>100个），推荐使用手动循环：

```cpp
Task<void> large_batch() {
    std::vector<Task<Response>> tasks;
    tasks.reserve(1000);
    
    // 批量创建
    for (int i = 0; i < 1000; ++i) {
        tasks.emplace_back(api_request(i));
    }
    
    // 批量等待
    std::vector<Response> responses;
    for (auto& task : tasks) {
        responses.push_back(co_await task);
    }
}
```

### when_any

等待多个任务中任意一个最先完成（事件驱动，取代早期 1ms 轮询实现，见 `when_any.h`）：

```cpp
template<typename... Tasks>
Task<std::pair<std::size_t, std::any>> when_any(Tasks&&... tasks);
```

返回 `{胜者索引, 结果}`：胜者索引为 0..N-1；结果存放在 `std::any` 中（异常也被封装进 `std::any`，`void` 结果存为空 `any`）。

```cpp
Task<void> race() {
    auto t1 = compute_value(1);
    auto t2 = compute_value(2);
    auto [index, result] = co_await when_any(t1, t2);  // 索引 + 结果
    std::cout << "最先完成: task" << index << std::endl;
}
```

**超时竞速**: `when_any_timeout(task, timeout)` 将超时建模为一个额外任务参与 race —— 胜者索引 0 表示原任务完成，1 表示超时。

**注意**: 落败的 selector 协程由 `CoroutineManager` 统一回收；`when_any` 内部对 selector `Task` 使用 `detach()` 放弃所有权，避免跨线程读取协程帧（TSAN 安全）。

---

## 4. sleep_for

协程友好的异步等待函数，不会阻塞线程。

### sleep_for函数签名

```cpp
template<typename Rep, typename Period>
auto sleep_for(std::chrono::duration<Rep, Period> duration);  // 返回 ClockAwaiter，可直接 co_await
```

### sleep_for基本用法

```cpp
Task<void> delayed_operation() {
    std::cout << "Starting..." << std::endl;
    
    // 异步等待1秒（不阻塞线程）
    co_await sleep_for(std::chrono::seconds(1));
    
    std::cout << "Done after 1 second!" << std::endl;
}
```

### 精度示例

```cpp
Task<void> precise_timing() {
    // 毫秒级精度
    co_await sleep_for(std::chrono::milliseconds(500));
    
    // 微秒级精度
    co_await sleep_for(std::chrono::microseconds(100));
    
    // 纳秒级精度（系统限制）
    co_await sleep_for(std::chrono::nanoseconds(1000));
}
```

### 定时任务

```cpp
Task<void> periodic_task() {
    for (int i = 0; i < 10; ++i) {
        do_work();
        co_await sleep_for(std::chrono::seconds(5));  // 每5秒执行一次
    }
}
```

### 与 std::this_thread::sleep_for 的区别

```cpp
// 错误：阻塞整个线程
Task<void> blocking_sleep() {
    std::this_thread::sleep_for(std::chrono::seconds(1));  // 阻塞线程
    co_return;
}

// 正确：协程友好的等待
Task<void> async_sleep() {
    co_await sleep_for(std::chrono::seconds(1));  // 不阻塞线程
    co_return;
}
```

---

## 5. Channel

协程间通信的线程安全通道（互斥锁 + 条件变量），支持生产者-消费者模式。

### Channel函数签名

```cpp
template<typename T>
class Channel {
public:
    explicit Channel(size_t capacity = 0);   // 0 = 无界缓冲（非阻塞）

    Task<bool> send(T value);                 // 发送，缓冲满时挂起；关闭后返回 false
    Task<std::optional<T>> recv();            // 接收，缓冲空时挂起；关闭且空时返回 nullopt

    void close();
    bool is_closed() const;
};
```

> 便捷工厂：`make_channel<T>(capacity)` 返回 `std::shared_ptr<Channel<T>>`（内部用内存池分配）。

### Channel基本用法

```cpp
Task<void> producer(Channel<int>& channel) {
    for (int i = 0; i < 100; ++i) {
        co_await channel.send(i);
        co_await sleep_for(std::chrono::milliseconds(10));
    }
    channel.close();
}

Task<void> consumer(Channel<int>& channel) {
    while (!channel.is_closed()) {
        auto value = co_await channel.recv();
        if (value.has_value()) {
            std::cout << "Received: " << *value << std::endl;
        }
    }
}

Task<void> channel_example() {
    Channel<int> channel(64);  // 容量64
    
    // 启动生产者和消费者
    auto prod = producer(channel);
    auto cons = consumer(channel);
    
    // 等待完成
    co_await when_all(prod, cons);
}
```

### 生产者-消费者模式

```cpp
// 多生产者单消费者
Task<void> multi_producer_example() {
    Channel<std::string> channel(128);
    
    // 启动多个生产者
    auto producer1 = data_producer("source1", channel);
    auto producer2 = data_producer("source2", channel);
    auto producer3 = data_producer("source3", channel);
    
    // 单个消费者
    auto consumer = data_consumer(channel);
    
    // 等待所有完成
    co_await when_all(producer1, producer2, producer3, consumer);
}

Task<void> data_producer(const std::string& name, Channel<std::string>& channel) {
    for (int i = 0; i < 50; ++i) {
        co_await channel.send(name + "_" + std::to_string(i));
        co_await sleep_for(std::chrono::milliseconds(20));
    }
}
```

### 多生产者多消费者

```cpp
Task<void> mpmc_example() {
    Channel<WorkItem> work_channel(256);
    Channel<Result> result_channel(256);
    
    // 多个工作者协程
    std::vector<Task<void>> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back(worker_coroutine(work_channel, result_channel));
    }
    
    // 工作分发器
    auto dispatcher = work_dispatcher(work_channel);
    
    // 结果收集器
    auto collector = result_collector(result_channel);
    
    // 等待所有协程
    co_await when_all(dispatcher, collector);
    for (auto& worker : workers) {
        co_await worker;
    }
}
```

### Channel性能特点

- **线程安全实现**: 基于互斥锁 + 条件变量保护缓冲与等待队列
- **可配置容量**: 固定容量防止内存无限增长（容量 0 表示无界）
- **背压支持**: 缓冲区满时发送者挂起等待
- **多线程安全**: 支持多生产者多消费者

### Channel适用场景

- **流水线处理**: 多阶段数据处理
- **工作队列**: 任务分发和处理
- **事件驱动**: 事件生产和消费

### Channel注意事项

- 发送协程在缓冲区满时会挂起
- 接收协程在缓冲区空时会挂起
- 关闭Channel后，剩余数据仍可接收，缓冲清空后再 recv 返回 nullopt
- 向已关闭的Channel发送数据会返回 `false`，不会抛出异常

---

## 6. 线程池

FlowCoro 提供后台线程池用于执行阻塞操作。

### 配置

线程池大小通过 `flowcoro::initialize()` 在启动时配置（`0` 表示按 `hardware_concurrency` 自动检测）：

```cpp
flowcoro::initialize(8);  // 8 个后台线程

// 查询当前线程池大小（运行时统计）
auto stats = flowcoro::get_runtime_stats();
size_t pool_size = stats.thread_pool_size;

// 程序退出前清理
flowcoro::shutdown();
```

### 线程池性能特点

- **后台执行**: 不占用协程调度器线程
- **阻塞操作友好**: 适合文件I/O、数据库访问
- **自动负载均衡**: 工作窃取算法
- **异常安全**: 异常传播到调用协程

### 内存池

FlowCoro 内置基于 Redis/Nginx 设计的内存池（`SimpleMemoryPool`，见 `memory_pool.h`），通过 `PoolAllocator` 为内部数据结构（如 Channel 缓冲、lockfree 队列/栈节点）提供快速分配：

```cpp
// 使用 PoolAllocator 的容器/对象
std::vector<int, flowcoro::PoolAllocator<int>> vec;

// Channel 内部缓冲即由 PoolAllocator 分配
auto ch = flowcoro::make_channel<int>(10);
```

### 对象池

`flowcoro::ObjectPool<T>` 提供对象复用（`acquire()` / `release()`，见 `object_pool.h`）：

```cpp
flowcoro::ObjectPool<ExpensiveObject> pool;   // 初始预分配 16 个对象

// 获取对象（空池时自动 new）
auto obj = pool.acquire();
obj->do_work();

// 用完后归还池中复用
pool.release(std::move(obj));
```

---

## 7. 内存管理

FlowCoro 实现了多层内存管理系统，确保高性能和低延迟。

### 简洁的语法糖

对于常见的批量等待模式，FlowCoro 提供语法糖：

```cpp
// 传统写法
Task<void> traditional_way() {
    auto task1 = fetch_data(1);
    auto task2 = fetch_data(2);
    auto task3 = fetch_data(3);
    
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
}

// 语法糖写法
Task<void> sugar_way() {
    auto [result1, result2, result3] = co_await when_all(
        fetch_data(1),
        fetch_data(2),
        fetch_data(3)
    );
}
```

### 批量处理的最佳实践

```cpp
Task<void> best_practice_batch() {
    // 1. 预分配容器
    std::vector<Task<Response>> tasks;
    tasks.reserve(expected_count);
    
    // 2. 批量创建任务
    for (const auto& request : requests) {
        tasks.emplace_back(process_request(request));
    }
    
    // 3. 批量等待结果
    std::vector<Response> responses;
    responses.reserve(tasks.size());
    
    for (auto& task : tasks) {
        responses.push_back(co_await task);
    }
    
    // 4. 处理结果
    process_responses(responses);
}
```

### 适用场景总结

FlowCoro 最适合以下场景：

- **需要同时获取多个任务结果时**
- **批量API调用和数据处理**
- **高并发Web服务器**
- **并行计算和数据转换**
- **I/O密集型应用**

---

## API 速查表

### Task协程任务接口

```cpp
// 创建和使用
Task<int> task = async_function();  // 同步执行直到挂起点
int result = co_await task;         // 等待结果
bool ready = task.is_ready();       // 检查是否完成
```

### sync_wait - 同步等待协程

```cpp
// 在main()或非协程函数中使用
int result = sync_wait(async_function());
```

### when_all - 批量等待语法糖

```cpp
// 固定数量任务
auto [r1, r2, r3] = co_await when_all(task1, task2, task3);

// 动态数量任务 - 使用循环
for (auto& task : tasks) {
    results.push_back(co_await task);
}
```

### sleep_for - 协程友好的异步等待

```cpp
co_await sleep_for(std::chrono::milliseconds(100));
co_await sleep_for(std::chrono::seconds(1));
```

### Channel - 协程间通信

```cpp
Channel<int> ch(capacity);
co_await ch.send(value);           // 发送数据
auto value = co_await ch.recv();   // 接收数据（关闭且空时返回 nullopt）
ch.close();                        // 关闭通道
```

### 线程池配置

```cpp
flowcoro::initialize(thread_count);            // 启动时设置线程池大小（0=自动）
auto rs = flowcoro::get_runtime_stats();       // 查询运行时统计（含 thread_pool_size）
```

---

## 8. 取消 (CancellationToken)

结构化取消支持（见 `cancellation.h`），用于跨多个子任务共享取消状态、在 `co_await` 路径上嵌入协作式取消检查点：

```cpp
flowcoro::CancellationTokenSource source;
auto token = source.token();   // 派发多个共享同一状态的 token

// 协程内：在取消点检查，已取消则抛 CancellationTokenException
co_await flowcoro::cancellation_point(token);

// 绑定到 Task：token 取消时该 Task 自动请求取消
task.attach_cancellation_token(token);

// 任意处触发取消（线程安全）
source.cancel();
```

主要接口（`CancellationToken` / `CancellationTokenSource`）：

```cpp
class CancellationToken {
    bool is_cancelled() const noexcept;
    void throw_if_cancellation_requested() const;  // 已取消则抛 CancellationTokenException
    void* register_callback(void (*fn)(void*), void* ctx) const;
    void unregister_callback(void* handle) const;
};
class CancellationTokenSource {
    CancellationToken token() const noexcept;
    void cancel() const;                    // 置取消标志并触发所有已注册回调
    bool is_cancelled() const noexcept;
};
```

---

## 9. 确定性实时执行 (RtExecutor)

`flowcoro::rt` 提供单线程亲和的确定性实时执行模型（见 `rt_executor.h`），适用于机器人/控制/嵌入式场景：

- **单线程确定性**: 所有 `resume`/`destroy` 都在 `run()` 的调用线程发生；跨线程事件只允许 `post_ready(h)` 递回句柄，绝不 inline `resume`
- **惰性启动**: `RtTask` 的 `initial_suspend = suspend_always`
- **协作式停止**: 周期边界查 `stop_token()`，`request_stop()` 后可优雅关停
- **两段式拆除**: `request_stop()` → `co_return` 到 `final_suspend`(park) → `run()` 在 executor 线程 destroy 帧 → `is_finished()`

```cpp
flowcoro::rt::RtExecutor ex{{ .pin_cpu = -1, .idle_sleep_us = 1000 }};

// 注册周期任务（惰性启动，executor 接管所有权）
auto task = []() -> flowcoro::rt::RtTask {
    while (!co_await flowcoro::rt::stop_requested()) {
        do_work();
        co_await flowcoro::rt::sleep_for(std::chrono::milliseconds(10));
        co_await flowcoro::rt::yield();
    }
}();
ex.spawn(std::move(task), "my_task");

ex.run_blocking();          // 或每 tick 调一次 ex.run() 直到 ex.is_finished()
```

主要接口（`RtExecutor`）:

```cpp
void spawn(RtTask task, std::string_view name);  // 注册任务（须与 run 同线程、不并发）
void run();                     // 非阻塞 tick：处理到期/取消 timer + 抽干 ready
void run_blocking();            // 阻塞循环 run() + idle 睡眠，直到 is_finished()
void request_stop() noexcept;   // 任意线程可调；置停止标志
void shutdown();                // 优雅关停：request_stop + 反复 run() 至 quiesce
void post_ready(Handle h) noexcept;  // 跨线程事件唯一合法出口
bool is_finished() const noexcept;
StopToken stop_token() const noexcept;
```

awaitable：`rt::sleep_for(d)`、`rt::yield()`、`rt::stop_requested()`（均须在 `RtTask` 协程内 `co_await`）。

---

## 高级特性

### 性能监控

FlowCoro 内置全局性能监控，统计任务的创建/完成/取消/失败、调度调用和定时器事件：

```cpp
// 方式一：运行时统计（含日志、内存、任务生命周期）
auto rs = flowcoro::get_runtime_stats();
std::cout << "Thread pool size: " << rs.thread_pool_size << std::endl;

// 方式二：性能监控器统计（PerformanceMonitor 单例）
auto stats = flowcoro::PerformanceMonitor::get_instance().get_stats();
std::cout << "Tasks created: " << stats.tasks_created << std::endl;
std::cout << "Tasks completed: " << stats.tasks_completed << std::endl;

// 方式三：打印协程池与全局统计（scheduler_api.h）
flowcoro::print_flowcoro_stats();
```

### 协程池

协程池由 `CoroutineManager` 自动管理（多调度器 + 无锁队列 + 负载均衡），无公开配置接口，无需手动配置。底层调度接口见 `scheduler_api.h`：

```cpp
flowcoro::schedule_coroutine_enhanced(handle);  // 调度一个协程句柄
flowcoro::drive_coroutine_pool();               // 驱动协程池
flowcoro::print_pool_stats();                   // 打印协程池统计
```

### 调试和诊断

```cpp
// 设置全局日志级别（logger.h）
flowcoro::GlobalLogger::get().set_level(flowcoro::LogLevel::TRACE);

Task<void> debug_example() {
    LOG_INFO("Starting operation");   // LOG_TRACE / LOG_DEBUG / LOG_INFO / ...
    
    co_await operation();
    
    LOG_INFO("Operation completed");
}
```

---

这份文档涵盖了FlowCoro的核心概念、API使用和最佳实践。FlowCoro的独特设计使其在高并发、批量处理场景下表现卓越，特别适合现代C++异步编程需求。
