# FlowCoro API 参考手册

**基于无锁队列的高性能协程调度系统**

>  **性能数据**: 详细性能指标和基准测试结果请参考 [性能数据参考](PERFORMANCE_DATA.md)

## 核心架构说明

FlowCoro 采用**三层调度架构**，结合无锁队列和智能负载均衡，专门优化高吞吐量批量任务处理：

- **调度方式**: suspend_never立即执行 → 协程池调度 → 无锁队列分发 → 线程池执行
- **适用场景**: 批量并发任务、Web API服务、独立任务处理
- **核心特性**: 高性能协程创建执行、专业并发控制、智能负载均衡

## 目录

### 核心概念
- [并发机制](#并发机制) - Task创建时并发，co_await只是等待
- [架构限制](#架构限制) - 不支持的使用模式
- [适用场景](#适用场景) - 推荐和不推荐的用法

### API参考
- [1. Task&lt;T&gt;](#1-taskt) - 协程任务接口
- [2. sync_wait()](#2-sync_wait) - 同步等待
- [3. when_all()](#3-when_all) - 批量等待语法糖
- [4. sleep_for()](#4-sleep_for) - 协程友好的延时
- [5. Channel&lt;T&gt;](#5-channelt) - 协程间通信通道
- [6. 线程池](#6-threadpool) - 后台任务处理
- [7. 内存管理](#7-memory) - 内存池和对象池

---

## 并发机制

### 唯一的并发方式

FlowCoro 只有**一种**并发方式：**Task创建时立即并发执行**

```cpp
Task<void> concurrent_processing() {
    //  正确：任务创建时立即开始并发执行
    auto task1 = async_compute(1);  // 立即开始执行
    auto task2 = async_compute(2);  // 立即开始执行
    auto task3 = async_compute(3);  // 立即开始执行
    
    // co_await 只是等待结果，不影响并发
    auto r1 = co_await task1;  // 等待结果
    auto r2 = co_await task2;  // 等待结果（可能已完成）
    auto r3 = co_await task3;  // 等待结果（可能已完成）
    
    co_return;
}
```

### when_all 语法糖

`when_all` 不是独立的并发机制，只是便利的语法糖：

```cpp
// when_all 的实际实现
template<typename... Tasks>
Task<std::tuple<...>> when_all(Tasks&&... tasks) {
    std::tuple<...> results;
    // 内部仍是顺序 co_await！
    ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
    co_return std::move(results);
}

// 使用示例
Task<void> batch_processing() {
    auto task1 = async_compute(1);  // 立即开始执行
    auto task2 = async_compute(2);  // 立即开始执行
    auto task3 = async_compute(3);  // 立即开始执行
    
    // 语法糖：简化结果获取
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1), std::move(task2), std::move(task3)
    );
    
    co_return;
}
```

## 架构限制

### 已解决的模式 (通过Channel)

**生产者-消费者协作:**

```cpp
// ✓ 正确：现在支持协程间通信
Task<void> producer_consumer() {
    auto channel = make_channel<int>(10);
    
    auto producer_task = [channel]() -> Task<void> {
        for (int i = 0; i < 10; ++i) {
            co_await channel->send(i);
        }
        channel->close();
    };
    
    auto consumer_task = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            process(value.value());
        }
    };
    
    auto prod = producer_task();
    auto cons = consumer_task();
    
    co_await prod;
    co_await cons;
}
```

**协程通信管道:**

```cpp
// ✓ 正确：Channel 支持流水线处理
Task<void> pipeline() {
    auto channel1 = make_channel<RawData>(5);
    auto channel2 = make_channel<ProcessedData>(5);
    
    auto stage1 = process_stage1(channel1, channel2);
    auto stage2 = process_stage2(channel2);
    
    co_await stage1;
    co_await stage2;
}
```

###  推荐的模式

**批量任务处理:**

```cpp
//  正确：批量处理模式
Task<void> batch_requests() {
    std::vector<Task<Response>> tasks;
    
    // 批量创建任务（立即开始并发执行）
    for (const auto& req : requests) {
        tasks.push_back(handle_request(req));
    }
    
    // 批量等待结果
    std::vector<Response> responses;
    for (auto& task : tasks) {
        responses.push_back(co_await task);
    }
    
    co_return;
}
```

## 适用场景

### 强烈推荐

- **Web API服务器**: 每个请求独立处理
- **批量数据处理**: 批量查询、文件处理
- **压力测试工具**: 大量并发请求
- **爬虫系统**: 并发网页抓取

###  不适合

- **实时系统**: 需要协程间持续通信
- **流处理**: 需要协程链式协作
- **事件驱动**: 需要协程间消息传递

---
    
    ---

## 1. Task&lt;T&gt;

FlowCoro 的核心协程任务类，封装协程的生命周期管理。

### 基本用法

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
    co_return;
}
```

### 主要方法

#### `T get()`

同步获取协程结果，会阻塞当前线程。

```cpp
Task<int> compute() {
    co_return 42;
}

int main() {
    auto task = compute();
    int result = task.get();  // 阻塞等待
    std::cout << result << std::endl;
    return 0;
}
```

#### `bool done() const`

检查协程是否已完成。

```cpp
auto task = compute_value(10);
if (task.done()) {
    std::cout << "Already completed" << std::endl;
}
```

### 生命周期

- **创建**: Task创建时协程立即开始执行
- **执行**: 在多线程环境中异步执行
- **等待**: 通过 `co_await` 或 `get()` 获取结果
- **销毁**: RAII自动管理资源

## 2. sync_wait()

同步等待协程完成的阻塞函数。

### 函数签名

```cpp
template<typename T>
T sync_wait(Task<T>&& task);

void sync_wait(Task<void>&& task);
```

### 基本用法

```cpp
Task<std::string> async_work() {
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return "Work completed";
}

int main() {
    auto task = async_work();
    
    // 同步等待完成
    std::string result = sync_wait(std::move(task));
    std::cout << result << std::endl;
    
    return 0;
}
```

### 注意事项

- `sync_wait` 是**同步阻塞**函数，会暂停当前线程执行
- 通常用于主函数或测试代码中
- 不要在协程内部使用，会导致死锁

## 3. when_all()

批量等待多个协程完成的语法糖函数。

### 函数签名

```cpp
template<typename... Tasks>
Task<std::tuple<decltype(tasks.get())...>> when_all(Tasks&&... tasks);
```

### 基本用法

```cpp
Task<int> compute1() { 
    co_await sleep_for(std::chrono::milliseconds(50));
    co_return 1; 
}

Task<int> compute2() { 
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return 2; 
}

Task<int> compute3() { 
    co_await sleep_for(std::chrono::milliseconds(75));
    co_return 3; 
}

Task<void> batch_example() {
    auto task1 = compute1();  // 立即开始执行
    auto task2 = compute2();  // 立即开始执行  
    auto task3 = compute3();  // 立即开始执行
    
    // 批量等待所有结果
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1),
        std::move(task2), 
        std::move(task3)
    );
    
    std::cout << "Results: " << r1 << ", " << r2 << ", " << r3 << std::endl;
    co_return;
}
```

### 适用场景

- **固定数量任务**: 2-10个已知的协程任务
- **结果解构**: 需要分别获取每个任务的结果
- **代码简洁**: 避免多个 `co_await` 调用

### 大量任务的替代方案

对于大量任务，推荐使用循环方式：

```cpp
Task<void> many_tasks() {
    std::vector<Task<int>> tasks;
    
    // 创建大量任务
    for (int i = 0; i < 1000; ++i) {
        tasks.push_back(compute_value(i));
    }
    
    // 逐个等待结果
    std::vector<int> results;
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
    
    co_return;
}
```

## 4. sleep_for()

协程友好的延时函数，不会阻塞工作线程。

### 函数签名

```cpp
Task<void> sleep_for(std::chrono::duration auto duration);
```

### 基本用法

```cpp
Task<void> delayed_work() {
    std::cout << "Start work" << std::endl;
    
    // 协程友好的延时，不阻塞线程
    co_await sleep_for(std::chrono::seconds(1));
    
    std::cout << "Work completed after 1 second" << std::endl;
    co_return;
}
```

### 精度示例

```cpp
Task<void> precise_timing() {
    auto start = std::chrono::high_resolution_clock::now();
    
    co_await sleep_for(std::chrono::milliseconds(100));
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Actual delay: " << duration.count() << "ms" << std::endl;
    co_return;
}
```

### 与 std::this_thread::sleep_for 的区别

```cpp
//  错误：阻塞工作线程
Task<void> blocking_sleep() {
    std::this_thread::sleep_for(std::chrono::seconds(1));  // 阻塞！
    co_return;
}

//  正确：非阻塞协程延时
Task<void> non_blocking_sleep() {
    co_await sleep_for(std::chrono::seconds(1));  // 非阻塞
    co_return;
}
```

## 5. Channel&lt;T&gt;

协程间通信通道，支持异步发送和接收消息，是实现生产者-消费者模式的核心组件。

### 函数签名

```cpp
template<typename T>
class Channel {
public:
    explicit Channel(size_t capacity = 0);
    
    Task<bool> send(T value);
    Task<std::optional<T>> recv();
    void close();
    bool is_closed() const;
};

template<typename T>
auto make_channel(size_t capacity = 0) -> std::shared_ptr<Channel<T>>;
```

### 基本用法

```cpp
Task<void> basic_channel_usage() {
    auto channel = make_channel<int>(5); // 缓冲区大小为5
    
    // 发送数据
    bool success = co_await channel->send(42);
    if (success) {
        std::cout << "Message sent successfully" << std::endl;
    }
    
    // 接收数据
    auto result = co_await channel->recv();
    if (result.has_value()) {
        std::cout << "Received: " << result.value() << std::endl;
    }
    
    channel->close();
    co_return;
}
```

### 生产者-消费者模式

```cpp
Task<void> producer_consumer_example() {
    auto channel = make_channel<std::string>(10);
    
    // 生产者协程
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            std::string message = "message_" + std::to_string(i);
            bool sent = co_await channel->send(message);
            if (!sent) {
                std::cout << "Channel closed, stop producing" << std::endl;
                break;
            }
            std::cout << "Produced: " << message << std::endl;
            co_await sleep_for(std::chrono::milliseconds(100));
        }
        channel->close();
    };
    
    // 消费者协程
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto message = co_await channel->recv();
            if (!message.has_value()) {
                std::cout << "Channel closed, stop consuming" << std::endl;
                break;
            }
            std::cout << "Consumed: " << message.value() << std::endl;
        }
    };
    
    auto prod_task = producer();
    auto cons_task = consumer();
    
    co_await prod_task;
    co_await cons_task;
    co_return;
}
```

### 多生产者多消费者

```cpp
Task<void> multi_producer_consumer() {
    auto channel = make_channel<int>(20);
    std::vector<Task<void>> tasks;
    
    // 创建多个生产者
    for (int producer_id = 0; producer_id < 3; ++producer_id) {
        auto producer = [channel, producer_id]() -> Task<void> {
            for (int i = 0; i < 10; ++i) {
                int value = producer_id * 100 + i;
                bool sent = co_await channel->send(value);
                if (!sent) break;
                std::cout << "Producer " << producer_id << " sent: " << value << std::endl;
                co_await sleep_for(std::chrono::milliseconds(50));
            }
        };
        tasks.push_back(producer());
    }
    
    // 创建多个消费者
    std::atomic<int> total_consumed{0};
    for (int consumer_id = 0; consumer_id < 2; ++consumer_id) {
        auto consumer = [channel, consumer_id, &total_consumed]() -> Task<void> {
            while (true) {
                auto value = co_await channel->recv();
                if (!value.has_value()) break;
                total_consumed.fetch_add(1);
                std::cout << "Consumer " << consumer_id << " got: " << value.value() << std::endl;
            }
        };
        tasks.push_back(consumer());
    }
    
    // 监控协程：当生产完成时关闭通道
    auto monitor = [channel]() -> Task<void> {
        co_await sleep_for(std::chrono::seconds(2)); // 等待生产完成
        channel->close();
    };
    tasks.push_back(monitor());
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        co_await task;
    }
    
    std::cout << "Total consumed: " << total_consumed.load() << std::endl;
    co_return;
}
```

### 性能特点

- **缓冲机制**: 支持有界缓冲区，避免过度内存使用
- **线程安全**: 完全线程安全，支持跨线程协程通信
- **零拷贝**: 内部使用移动语义，避免不必要的数据拷贝
- **背压控制**: 当缓冲区满时，发送操作会挂起协程

### 适用场景

- 协程间数据传递
- 生产者-消费者模式
- 数据流水线处理
- 事件驱动架构
- 限流和背压控制

### 注意事项

```cpp
// ✓ 正确：及时关闭通道
auto channel = make_channel<int>(10);
// ... 使用通道
channel->close(); // 通知接收者不再有数据

// ✗ 错误：忘记关闭可能导致接收者永久等待
auto channel = make_channel<int>(10);
// ... 发送完数据后忘记关闭
// 接收者的 recv() 可能永久挂起
```

## 6. 线程池

FlowCoro 内置高性能线程池，自动处理协程调度。

### 配置

```cpp
// 线程池会根据 CPU 核心数自动配置
// 通常为 32-128 个工作线程

```

### 性能特点

- **智能调度**: 自动负载均衡
- **工作窃取**: 高效的任务分发
- **跨线程安全**: 协程可在任意线程恢复

## 6. 内存管理

### 动态内存池

```cpp
#include "flowcoro/memory_pool.h"

// 创建内存池
MemoryPool pool(64, 100);  // 64字节块，初始100个
pool.set_expansion_factor(2.0);  // 扩展倍数

// 分配内存
void* ptr = pool.allocate();  // 保证成功
pool.deallocate(ptr);         // 释放内存
```

### 对象池

```cpp
#include "flowcoro/object_pool.h"

// 创建对象池
ObjectPool<MyClass> obj_pool(50);  // 初始50个对象

// 获取对象
auto obj = obj_pool.acquire();  // 智能指针包装
// 对象在智能指针销毁时自动归还池中
```

---
```

**适用场景：**
- 所有异步编程场景
- 有依赖关系的流水线处理
- 独立任务的并发处理
- 高性能异步I/O操作

### when_all 语法糖

**本质：** co_await 的便利封装，简化多任务等待语法

```cpp
// when_all 的实际实现（来自 core.h）
template<typename... Tasks>
Task<std::tuple<...>> when_all(Tasks&&... tasks) {
    std::tuple<...> results;
    
    // 内部仍然是顺序 co_await，不是并发！
    ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
    
    co_return std::move(results);
}

// 使用示例
Task<void> when_all_demo() {
    auto task1 = async_compute(1);
    auto task2 = async_compute(2);
    auto task3 = async_compute(3);
    
    // 语法糖：简化多结果获取
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1),
        std::move(task2),
        std::move(task3)
    );
    
    // 完全等价于：
    // auto r1 = co_await task1;
    // auto r2 = co_await task2; 
    // auto r3 = co_await task3;
    
    co_return;
}
```

**适用场景：**
- 需要同时获取多个任务结果时
- 避免手动解构多个co_await调用
- 代码可读性优化

**重要澄清：**

| 常见误解 | 实际情况 |
|----------|----------|
| when_all 提供并发机制 | when_all 只是语法糖 |
| co_await 是串行等待 | co_await 等待已并发的任务 |
| FlowCoro 有多种并发模式 | FlowCoro 只有一种并发方式 |
| when_all 性能更高 | when_all 和 co_await 性能相同 |

---

## 1. 协程核心 (core.h)

FlowCoro v4.0.0 提供精简的协程任务接口，专注于核心功能。

### Task<T> - 协程任务接口

FlowCoro的协程任务模板类，提供基础的协程功能。

```cpp
template<typename T = void>
struct Task {
public:
    // 基础操作
    bool done() const noexcept; // 协程是否完成
    T get(); // 获取结果（阻塞执行）
    T get_result(); // 获取结果（兼容方法）

    // 状态检查
    bool is_ready() const noexcept; // 任务是否就绪

    // 协程等待接口
    bool await_ready() const;
    bool await_suspend(std::coroutine_handle<> waiting_handle);
    T await_resume();

    // 移动语义
    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;

    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

private:
    std::coroutine_handle<promise_type> handle;
    // 包含ThreadSafeCoroutineState和SafeCoroutineHandle
};
```

#### Task基础使用示例

```cpp
// 创建协程任务
Task<int> compute(int x) {
    co_return x * x;
}

// 方法1: 使用get()方法获取结果（同步阻塞）
auto task = compute(5);
auto result = task.get(); // result = 25

// 方法2: 使用sync_wait辅助函数
auto result2 = sync_wait(std::move(compute(5))); // result2 = 25

// 协程组合
Task<int> chain_compute() {
    auto result1 = co_await compute(3);
    auto result2 = co_await compute(result1);
    co_return result2; // 返回 81
}
```

### sync_wait - 同步等待协程

**sync_wait是同步阻塞的**，它将异步协程转换为同步调用，会阻塞当前线程直到协程完成。

```cpp
template<typename T>
T sync_wait(Task<T> task);
```

**重要说明：**
- `sync_wait` 是**同步阻塞**函数，会暂停当前线程执行
- 内部调用 `task.get()`，该方法通过 `handle.resume()` 直接执行协程
- 适用于主函数或需要同步等待结果的场景
- **不适合**在协程内部使用（会阻塞协程调度）

#### 使用场景对比

```cpp
// 正确：在main函数中使用sync_wait
int main() {
    
    // 同步阻塞等待协程完成
    auto result = sync_wait(std::move(async_compute(21))); // 阻塞主线程
    std::cout << result << std::endl; // result = 42
    return 0;
}

// 错误：在协程中使用sync_wait（阻塞协程调度）
Task<void> bad_example() {
    // 这会阻塞整个协程调度器！
    auto result = sync_wait(std::move(async_compute(21))); // 不要这样做
    co_return;
}

// 正确：在协程中使用co_await（异步非阻塞）
Task<void> good_example() {
    // 异步等待，不阻塞协程调度器
    auto result = co_await async_compute(21); // 异步执行
    std::cout << result << std::endl;
    co_return;
}
```

### when_all - 批量等待语法糖

简化多个已并发执行任务的结果收集，when_all本身不提供并发机制。

```cpp
template<typename... Tasks>
auto when_all(Tasks&&... tasks) -> WhenAllAwaiter<Tasks...>;
```

#### 基础使用

```cpp
// 不同类型的协程任务  
Task<int> compute_int(int x) {
    co_return x * x;
}

Task<std::string> compute_string(const std::string& s) {
    co_return s + "_processed";
}

Task<bool> compute_bool() {
    co_return true;
}

// 并发执行多个不同类型的任务
Task<void> example_mixed_types() {
    auto task1 = compute_int(5);
    auto task2 = compute_string("hello");
    auto task3 = compute_bool();

    auto [int_result, str_result, bool_result] =
        co_await when_all(
            std::move(task1),    // 返回 25
            std::move(task2),    // 返回 "hello_processed"
            std::move(task3)     // 返回 true
        );

    std::cout << "整数结果: " << int_result << std::endl;
    std::cout << "字符串结果: " << str_result << std::endl;
    std::cout << "布尔结果: " << bool_result << std::endl;
}
```

#### 相同类型任务

```cpp
// 相同类型的协程任务
Task<int> heavy_compute(int x) {
    // 模拟计算密集型任务
    co_return x * x + x;
}

Task<void> example_same_types() {
    // 创建任务
    auto task1 = heavy_compute(10);
    auto task2 = heavy_compute(20);
    auto task3 = heavy_compute(30);
    auto task4 = heavy_compute(40);

    // 并发执行多个相同类型的任务
    auto [r1, r2, r3, r4] = co_await when_all(
        std::move(task1), // 返回 110
        std::move(task2), // 返回 420
        std::move(task3), // 返回 930
        std::move(task4)  // 返回 1640
    );

    std::cout << "结果: " << r1 << ", " << r2 << ", " << r3 << ", " << r4 << std::endl;
}
```

#### 使用场景

**适合 when_all 的场景:**
- **固定数量任务**: 2-10个已知的协程任务
- **并行处理**: 需要同时等待多个独立操作完成
- **资源聚合**: 从多个源获取数据并组合结果
- **批量操作**: 并行执行固定数量的相似操作

**不适合 when_all 的场景:**
- **大量任务**: 超过百个任务（推荐使用协程池）
- **动态任务**: 运行时确定任务数量
- **流式处理**: 需要处理任务完成顺序的场景
- **高并发场景**: 大量任务必须使用协程池管理

#### 性能特点

- **零开销抽象**: 编译期优化，无运行时开销
- **类型安全**: 强类型返回值，编译期检查
- **内存高效**: 栈上分配，无动态内存分配
- **适度并发**: 最优性能区间 2-10 个任务

#### ️ 重要限制：when_all 没有协程池

**when_all 使用的是线程池，不是协程池！**

```cpp
// when_all 内部实现使用 GlobalThreadPool
template<typename... Tasks>
class WhenAllAwaiter {
    void process_task(TaskType& task, auto complete_callback) {
        GlobalThreadPool::get().enqueue([&task, complete_callback]() {
            // 在线程池中执行，失去协程优势
        });
    }
};
```

**对于大规模并发（如5000个任务），必须使用协程池：**

```cpp
// 错误：when_all 无法处理大量任务
// auto result = co_await when_all(task1, task2, ..., task5000); // 编译失败！

// 正确：使用协程池处理大量任务
flowcoro::Task<void> handle_many_tasks() {
    const int TASK_COUNT = 5000;
    const int MAX_CONCURRENT = 50;

    std::queue<int> task_queue;
    std::atomic<int> active_tasks{0};

    // 填充任务队列
    for (int i = 0; i < TASK_COUNT; ++i) {
        task_queue.push(i);
    }

    // 协程池管理
    while (!task_queue.empty() || active_tasks.load() > 0) {
        // 控制并发数
        while (active_tasks.load() < MAX_CONCURRENT && !task_queue.empty()) {
            int task_id = task_queue.front();
            task_queue.pop();
            active_tasks.fetch_add(1);

            // 调度到协程池
            flowcoro::get_coroutine_manager().schedule_resume(
                [task_id, &active_tasks]() -> flowcoro::Task<void> {
                    auto result = co_await compute_task(task_id);
                    // 处理结果...
                    active_tasks.fetch_sub(1);
                }().get_handle()
            );
        }

        // 驱动协程执行
        flowcoro::drive_coroutines();
        co_await flowcoro::Task<void>([](){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }
}
```

#### 错误处理

```cpp
Task<int> may_fail(int x) {
    if (x < 0) {
        throw std::invalid_argument("负数不支持");
    }
    co_return x * 2;
}

Task<void> error_handling_example() {
    try {
        auto [r1, r2] = co_await when_all(
            may_fail(10), // 成功: 20
            may_fail(-5) // 抛出异常
        );
        // 不会执行到这里
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }
}
```

### sleep_for - 协程友好的异步等待

FlowCoro提供真正协程友好的sleep_for实现，使用定时器系统而非阻塞线程。

```cpp
auto sleep_for(std::chrono::milliseconds duration);
```

#### 实现机制

**sleep_for使用协程友好的定时器系统：**

```cpp
// 内部实现机制
class CoroutineFriendlySleepAwaiter {
    bool await_suspend(std::coroutine_handle<> h) {
        // 1. 挂起当前协程，不阻塞工作线程
        // 2. 添加定时器到 CoroutineManager 的定时器队列
        auto& manager = CoroutineManager::get_instance();
        auto when = std::chrono::steady_clock::now() + duration_;
        manager.add_timer(when, h);
        
        // 3. 返回true，让协程调度器继续处理其他协程
        return true;
    }
};
```

#### 关键特性

- **非阻塞**: 不占用工作线程，允许其他协程继续执行
- **精确定时**: 基于 `std::chrono::steady_clock` 实现精确计时
- **线程安全**: 定时器队列使用互斥锁保护，支持多线程安全访问
- **真正异步**: 协程挂起后，定时器线程负责在指定时间恢复协程

#### 使用示例

```cpp
Task<void> timing_example() {
    std::cout << "开始等待..." << std::endl;
    
    // 这不会阻塞工作线程！
    // 1. 协程挂起，工作线程继续处理其他协程
    // 2. 定时器在1秒后恢复这个协程
    co_await sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "等待完成！" << std::endl;
    co_return;
}

// 与传统阻塞sleep的对比
void traditional_blocking_sleep() {
    //  阻塞整个线程，其他协程无法执行
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

Task<void> coroutine_friendly_sleep() {
    //  协程友好，其他协程可以继续执行
    co_await sleep_for(std::chrono::milliseconds(1000));
}
```

### 协程管理函数

```cpp
// 调度协程到多线程协程池
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);

// 获取协程管理器实例
CoroutineManager& get_coroutine_manager();

// 驱动协程执行（主要用于定时器和清理任务）
void drive_coroutines();
```

---

## 2. 线程池 (thread_pool.h)

高性能线程池实现，用于CPU密集型任务。

### ThreadPool - 线程池类

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency());
    ~ThreadPool();

    // 提交任务
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    // 获取线程数
    size_t size() const noexcept;

    // 关闭线程池
    void shutdown();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};
```

#### 使用示例

```cpp
// 创建线程池
ThreadPool pool(4);

// 提交CPU密集型任务
auto future = pool.submit([](int x) {
    return x * x;
}, 42);

// 获取结果
auto result = future.get(); // result = 1764
```

---

## 3. 内存管理 (memory.h)

高效的内存池和对象池实现。

### MemoryPool - 内存池

```cpp
class MemoryPool {
public:
    MemoryPool(size_t block_size, size_t initial_blocks = 16);
    ~MemoryPool();

    // 分配内存
    void* allocate();

    // 释放内存
    void deallocate(void* ptr);

    // 获取统计信息
    size_t allocated_blocks() const;
    size_t total_blocks() const;

private:
    size_t block_size_;
    std::vector<void*> free_blocks_;
    std::vector<std::unique_ptr<char[]>> chunks_;
    std::mutex mutex_;
};
```

### ObjectPool - 对象池

```cpp
template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_size = 8);

    // 获取对象
    std::unique_ptr<T> acquire();

    // 释放对象
    void release(std::unique_ptr<T> obj);

    // 预分配对象
    void reserve(size_t count);

private:
    std::stack<std::unique_ptr<T>> available_;
    std::mutex mutex_;
};
```

#### 使用示例

```cpp
// 内存池使用
MemoryPool pool(64, 16); // 64字节块，16个初始块
void* ptr = pool.allocate();
// 使用内存...
pool.deallocate(ptr);

// 对象池使用
ObjectPool<MyClass> obj_pool(8);
auto obj = obj_pool.acquire();
// 使用对象...
obj_pool.release(std::move(obj));
```

---

## 4. 无锁数据结构 (lockfree.h)

无锁数据结构实现，用于高性能并发场景。

### LockfreeQueue - 无锁队列

```cpp
template<typename T>
class LockfreeQueue {
public:
    LockfreeQueue(size_t capacity = 1024);

    // 入队操作
    bool enqueue(const T& item);
    bool enqueue(T&& item);

    // 出队操作
    bool dequeue(T& item);

    // 状态查询
    bool empty() const;
    size_t size() const;

private:
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    size_t capacity_;
};
```

### LockfreeStack - 无锁栈

```cpp
template<typename T>
class LockfreeStack {
public:
    LockfreeStack();

    // 入栈操作
    void push(const T& item);
    void push(T&& item);

    // 出栈操作
    bool pop(T& item);

    // 状态查询
    bool empty() const;

private:
    std::atomic<Node*> head_;
};
```

#### 使用示例

```cpp
// 无锁队列
lockfree::Queue<int> queue;
queue.enqueue(42);

int value;
if (queue.dequeue(value)) {
    std::cout << "获取到值: " << value << std::endl;
}

// 无锁栈
lockfree::Stack<std::string> stack;
stack.push("Hello");
stack.push("World");

std::string item;
while (stack.pop(item)) {
    std::cout << item << " ";
}
```

---

## 5. 协程池

协程调度和管理系统，基于多线程安全设计，支持跨线程协程恢复。

### 多线程协程调度架构

FlowCoro v4.0.0 采用多线程协程调度架构：

```text
应用层协程
    ↓
CoroutineManager（协程管理器）
    ↓
CoroutinePool（多线程协程池）
    ↓
ThreadPool（32-128个工作线程）
```

### CoroutineManager - 协程管理器

```cpp
class CoroutineManager {
public:
    static CoroutineManager& get_instance();

    // 多线程安全的协程调度
    void schedule_resume(std::coroutine_handle<> handle);

    // 驱动协程执行（主要处理定时器）
    void drive();

    // 添加定时器
    void add_timer(std::chrono::steady_clock::time_point when, 
                   std::coroutine_handle<> handle);

    // 获取统计信息
    size_t active_coroutines() const;
    size_t pending_coroutines() const;

private:
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex queue_mutex_;
};
```

### 多线程安全保障

FlowCoro 使用以下机制确保多线程环境下的协程安全：

#### SafeCoroutineHandle - 线程安全协程句柄

```cpp
class SafeCoroutineHandle {
public:
    explicit SafeCoroutineHandle(std::coroutine_handle<> h);
    
    // 多线程安全的恢复
    void resume();
    
    // 线程安全的调度
    void schedule_resume();
    
    // 检查协程状态
    bool done() const;
    
private:
    std::shared_ptr<ThreadSafeCoroutineState> state_;
    std::shared_ptr<std::atomic<std::coroutine_handle<>>> handle_;
};
```

#### 执行状态保护

```cpp
class ThreadSafeCoroutineState {
    std::atomic<bool> running_{false};      // 防止并发执行
    std::atomic<bool> destroyed_{false};    // 生命周期标记
    std::atomic<bool> scheduled_{false};    // 防止重复调度
    std::shared_ptr<std::mutex> execution_mutex_; // 执行锁
};
```

### 协程调度函数

```cpp
// 调度协程到多线程协程池
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);

// 获取协程管理器
CoroutineManager& get_coroutine_manager();

// 驱动协程池（处理定时器和清理任务）
void drive_coroutine_pool();
```

---

## 6. 全局配置

系统配置和初始化。

### 全局配置项

```cpp
// 日志级别配置
enum class LogLevel {
    TRACE, DEBUG, INFO, WARN, ERROR, FATAL
};

// 设置日志级别
void set_log_level(LogLevel level);


// 清理协程系统
void cleanup_coroutine_system();
```

---

## 并发特性和性能

### 真正的多线程并发

FlowCoro v4.0.0 实现了真正的多线程协程并发：

```cpp
// 即使看起来是"顺序"执行，实际上是多线程并发的
flowcoro::Task<void> concurrent_processing() {
    std::vector<flowcoro::Task<int>> tasks;
    
    // 创建1000个协程任务
    for (int i = 0; i < 1000; ++i) {
        tasks.push_back(async_calculate(i, i * 2));
    }
    
    // 看起来是顺序等待，实际上在32-128个线程中并发执行
    std::vector<int> results;
    for (auto& task : tasks) {
        auto result = co_await task;  // 多线程并发执行
        results.push_back(result);
    }
    
    std::cout << "并发处理了 " << results.size() << " 个任务" << std::endl;
}
```

### 性能指标

基于多线程协程调度的实测性能：

| 指标 | FlowCoro v4.0 | 传统多线程 | 性能提升 |
|------|---------------|------------|----------|
| 执行时间 | 13ms | 778ms | **60倍** |
| 内存使用 | 792KB | 2.86MB | **4倍效率** |
| 线程数量 | 32-128个 | 10000个 | **99%节省** |
| 吞吐量 | 769K/s | 12K/s | **64倍** |

### 关键技术特性

- **多线程安全**: SafeCoroutineHandle 确保跨线程访问安全
- **真正并发**: 协程在多个工作线程中真正并行执行
- **智能调度**: 自动负载均衡，最大化 CPU 利用率
- **低开销**: 协程切换成本远低于线程上下文切换

---

## 完整使用示例

### 基础协程应用

```cpp
#include "flowcoro.hpp"
using namespace flowcoro;

// 异步计算任务
Task<int> async_calculate(int x, int y) {
    // 模拟一些异步工作
    co_return x + y;
}

// 协程组合
Task<int> complex_calculation() {
    auto result1 = co_await async_calculate(10, 20);
    auto result2 = co_await async_calculate(result1, 30);
    co_return result2; // 返回 60
}

int main() {
    // 执行协程（注意：sync_wait需要move语义）
    auto task = complex_calculation();
    auto result = task.get(); // 或者使用 sync_wait(std::move(task))
    std::cout << "计算结果: " << result << std::endl;

    return 0;
}
```

### 内存池应用

```cpp
#include "flowcoro.hpp"

int main() {
    // 创建内存池
    flowcoro::MemoryPool pool(1024, 16); // 1KB块，16个初始块

    // 分配内存
    void* buffer1 = pool.allocate();
    void* buffer2 = pool.allocate();

    // 使用内存...

    // 释放内存
    pool.deallocate(buffer1);
    pool.deallocate(buffer2);

    return 0;
}
```

### 无锁数据结构应用

```cpp
#include "flowcoro.hpp"
#include <thread>

int main() {
    flowcoro::lockfree::Queue<int> queue;

    // 生产者线程
    std::thread producer([&queue]() {
        for (int i = 0; i < 100; ++i) {
            queue.enqueue(i);
        }
    });

    // 消费者线程
    std::thread consumer([&queue]() {
        int value;
        int count = 0;
        while (count < 100) {
            if (queue.dequeue(value)) {
                std::cout << "处理: " << value << std::endl;
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

---

## 实用模式和最佳实践

### Web API 服务器模式

```cpp
struct Request {
    int user_id;
    std::string action;
};

struct Response {
    int status;
    std::string data;
};

Task<Response> handle_request(const Request& req) {
    // 模拟数据库查询
    co_await sleep_for(std::chrono::milliseconds(50));
    
    Response resp;
    resp.status = 200;
    resp.data = "User " + std::to_string(req.user_id) + " processed";
    co_return resp;
}

Task<void> web_server_simulation() {
    std::vector<Request> requests = {
        {1, "GET"}, {2, "POST"}, {3, "PUT"}, {4, "DELETE"}
    };
    
    std::vector<Task<Response>> tasks;
    
    // 批量创建任务（立即开始并发执行）
    for (const auto& req : requests) {
        tasks.push_back(handle_request(req));
    }
    
    // 批量等待结果
    for (auto& task : tasks) {
        auto response = co_await task;
        std::cout << "Status: " << response.status 
                  << ", Data: " << response.data << std::endl;
    }
    
    co_return;
}
```

### 数据批量处理模式

```cpp
Task<std::string> process_data(int id) {
    // 模拟数据处理
    co_await sleep_for(std::chrono::milliseconds(20));
    return "Processed data " + std::to_string(id);
}

Task<void> batch_data_processing() {
    const int batch_size = 100;
    std::vector<Task<std::string>> tasks;
    
    // 创建批量任务
    for (int i = 0; i < batch_size; ++i) {
        tasks.push_back(process_data(i));
    }
    
    // 处理结果
    std::vector<std::string> results;
    results.reserve(batch_size);
    
    for (auto& task : tasks) {
        results.push_back(co_await task);
    }
    
    std::cout << "Processed " << results.size() << " items" << std::endl;
    co_return;
}
```

### Channel流水线处理

```cpp
Task<void> producer_consumer_pipeline() {
    // 创建有缓冲的通道
    auto raw_data_channel = make_channel<std::string>(10);
    auto processed_data_channel = make_channel<int>(10);
    
    // 数据生产者
    auto producer = [raw_data_channel]() -> Task<void> {
        for (int i = 0; i < 20; ++i) {
            std::string data = "data_" + std::to_string(i);
            bool sent = co_await raw_data_channel->send(data);
            if (!sent) break; // 通道已关闭
            
            std::cout << "生产数据: " << data << std::endl;
            co_await sleep_for(std::chrono::milliseconds(50));
        }
        raw_data_channel->close();
        std::cout << "生产者完成" << std::endl;
    };
    
    // 数据处理器
    auto processor = [raw_data_channel, processed_data_channel]() -> Task<void> {
        while (true) {
            auto raw_data = co_await raw_data_channel->recv();
            if (!raw_data.has_value()) break; // 通道已关闭
            
            // 处理数据
            std::string data = raw_data.value();
            int processed = std::stoi(data.substr(5)); // "data_5" -> 5
            
            bool sent = co_await processed_data_channel->send(processed);
            if (!sent) break;
            
            std::cout << "处理数据: " << data << " -> " << processed << std::endl;
            co_await sleep_for(std::chrono::milliseconds(30));
        }
        processed_data_channel->close();
        std::cout << "处理器完成" << std::endl;
    };
    
    // 最终消费者
    auto consumer = [processed_data_channel]() -> Task<void> {
        int total = 0;
        int count = 0;
        
        while (true) {
            auto data = co_await processed_data_channel->recv();
            if (!data.has_value()) break; // 通道已关闭
            
            int value = data.value();
            total += value;
            count++;
            
            std::cout << "消费数据: " << value << " (累计: " << total << ")" << std::endl;
            co_await sleep_for(std::chrono::milliseconds(20));
        }
        
        std::cout << "消费者完成，总计: " << total << ", 数量: " << count << std::endl;
    };
    
    // 启动所有协程
    auto prod_task = producer();
    auto proc_task = processor();
    auto cons_task = consumer();
    
    // 等待流水线完成
    co_await prod_task;
    co_await proc_task;
    co_await cons_task;
    
    std::cout << "流水线处理完成" << std::endl;
    co_return;
}
```

### 性能最佳实践

1. **预分配容器**
```cpp
// 好的做法
std::vector<Task<int>> tasks;
tasks.reserve(expected_size);  // 预分配避免重复分配
```

2. **批量操作**
```cpp
// 批量创建后批量等待，而不是交替创建和等待
for (int i = 0; i < N; ++i) {
    tasks.push_back(async_operation(i));
}
for (auto& task : tasks) {
    results.push_back(co_await task);
}
```

3. **避免在协程内使用sync_wait**
```cpp
// 错误：在协程内调用sync_wait
Task<void> bad_example() {
    auto task = some_async_operation();
    sync_wait(task);  // 这会阻塞协程调度
}

// 正确：直接使用co_await
Task<void> good_example() {
    auto result = co_await some_async_operation();  // 非阻塞等待
}
```

---

**注意**: 这是FlowCoro v4.0.0的API文档，专注于核心协程功能。如需了解完整的项目信息，请参考主README文档。
