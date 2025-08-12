# FlowCoro 协程调度系统架构文档

**基于无锁队列的高性能协程调度系统**

## 架构概述

FlowCoro 采用**多层调度架构**，结合无锁队列和智能负载均衡，专门为高吞吐量批量任务处理优化。最新测试显示47万请求/秒的极限性能。

### 核心设计理念

- **无锁队列调度**: 基于lock-f### 第三层：线程池 (ThreadPool)

**职责**: 底层工作线程管理、任务执行

```cpp
class ThreadPool {
private:
    // 无锁任务队列
    lockfree::Queue<std::function<void()>> task_queue_;
    
    // 工作线程数组
    std::vector<std::thread> workers_;
    
    // 活跃线程计数
    std::atomic<size_t> active_threads_;
    
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        // 创建工作线程
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
                std::this_thread::yield();  // CPU友好的等待
            }
        }
    }
};
```

**特点**:
- **无锁队列**: 使用lockfree::Queue避免锁竞争
- **自适应线程数**: 根据CPU核心数调整工作线程数量
- **CPU友好**: 空闲时使用yield而非spin-wait结构的高性能任务分发
- **智能负载均衡**: 自适应调度器选择，最小化队列长度差异
- **多层调度架构**: 协程管理器 + 协程池 + 线程池的三层设计
- **Task立即执行**: 通过`suspend_never`实现任务创建时立即投递到调度系统

### 架构特点和性能指标

| 特点 | 说明 | 性能指标 |
|------|------|----------|
| **无锁调度** | lockfree::Queue实现任务分发 | 9.61M ops/s队列操作 |
| **立即执行** | Task创建时通过suspend_never立即投递 | 4.20M ops/s协程创建执行 |
| **智能负载均衡** | 自适应选择最优调度器 | 0.1μs调度决策时间 |
| **多线程安全** | 跨线程协程恢复支持 | 36.77M ops/s HTTP处理性能 |
| **continuation机制** | final_suspend实现任务链 | 零拷贝任务传递 |
| **continuation模式** | final_suspend支持协程链 | 异步任务协作支持 |

## 三层调度架构

FlowCoro 采用三层调度架构，实现高性能协程调度：

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
namespace lockfree {
class ThreadPool {
private:
    // 无锁任务队列
    lockfree::Queue<std::function<void()>> task_queue_;
    
    // 工作线程数组
    std::vector<std::thread> workers_;
    
    // 原子停止标志
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_threads_{0};
    
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
}
```

**特点**:

- **无锁队列**: 使用lock-free Queue避免线程竞争
- **工作线程**: 通常32-128个工作线程
- **轻量级调度**: 简单的轮询+yield机制
- **跨线程安全**: 支持协程在不同线程间恢复

## 协程执行流程

### 1. 任务创建和立即执行机制

```cpp
// Task创建时的执行流程
## 协程执行流程

### 完整执行链路

```text
1. Task创建
   ↓ suspend_never 立即执行
2. 协程管理器调度决策
   ↓ schedule_coroutine_enhanced
3. 智能负载均衡器选择
   ↓ 选择负载最轻的调度器
4. 协程池调度器入队
   ↓ lockfree::Queue.enqueue
5. 线程池工作线程获取
   ↓ lockfree::Queue.dequeue
6. 协程在工作线程执行
   ↓ handle.resume()
7. final_suspend处理continuation
   ↓ 恢复等待的协程
8. 任务完成，结果返回
```

### 1. 任务创建和立即执行

```cpp
// Task创建时立即开始执行
auto task = compute_async(42);  // suspend_never → 立即投递到调度系统

template<typename T>
struct Task {
    struct promise_type {
        // 关键：suspend_never实现立即执行
        std::suspend_never initial_suspend() { return {}; }
        
        // continuation机制实现任务链
        auto final_suspend() noexcept {
            struct final_awaiter {
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    if (promise->continuation) {
                        return promise->continuation;  // 恢复等待的协程
                    }
                    return std::noop_coroutine();
                }
            };
            return final_awaiter{this};
        }
    };
};
```

### 2. 调度决策和负载均衡

```cpp
// 智能负载均衡器选择最优调度器
size_t SmartLoadBalancer::select_scheduler() noexcept {
    thread_local size_t quick_choice = 0;
    quick_choice = (quick_choice + 1) % count;
    
    // 每16次进行一次负载均衡检查
    if ((quick_choice & 0xF) == 0) {
        size_t min_load = SIZE_MAX;
        size_t best_scheduler = 0;
        
        for (size_t i = 0; i < count; ++i) {
            size_t load = queue_loads_[i].load(std::memory_order_relaxed);
            if (load < min_load) {
                min_load = load;
                best_scheduler = i;
            }
        }
        return best_scheduler;
    }
    
    return quick_choice;  // 大部分时候使用轮询，减少开销
}
```

### 3. 无锁队列操作

```cpp
// 协程入队到无锁队列
void enqueue(std::coroutine_handle<> handle) {
    if (destroyed.load(std::memory_order_acquire)) {
        return; // 队列已析构，丢弃任务
    }

    Node* new_node = new Node;
    auto* task_ptr = new std::function<void()>([handle]() {
        if (handle && !handle.done()) {
            handle.resume();  // 在工作线程中恢复协程
        }
    });
    new_node->data.store(task_ptr);

    Node* prev_tail = tail.exchange(new_node);
    if (prev_tail) {
        prev_tail->next.store(new_node);
    }
}
```

// 调用端
auto task = async_compute(42);  // 此时任务已经开始执行
auto result = co_await task;    // 等待结果（可能已经完成）
```

**执行时序**:

```text
1. Task创建 -> promise_type构造 -> initial_suspend() returns suspend_never
2. 协程立即开始执行 -> 投递到CoroutineManager
3. CoroutineManager -> 选择最优调度器 -> 投递到协程池
4. 协程池调度器 -> 入队到无锁队列 -> 线程池工作线程执行
5. 协程完成 -> final_suspend() -> 恢复等待的协程
```
```

### 2. 调度决策链

```text
Task 创建 (suspend_never)
  ↓
立即开始执行协程体
  ↓
遇到 co_await 挂起
  ↓
await_suspend() 调用
  ↓
CoroutineManager::schedule_resume()
  ↓
CoroutinePool::schedule_coroutine()
  ↓
调度器选择和负载均衡
  ↓
工作线程执行 handle.resume()
```

### 3. 结果获取流程

```cpp
// 等待结果的两种方式

// 方式1: 协程内等待
Task<void> async_wait() {
    auto task = async_compute(42);
    int result = co_await task;  // 异步等待
    co_return;
}

// 方式2: 同步等待
int main() {
    auto task = async_compute(42);
    int result = task.get();  // 同步阻塞等待
    return 0;
}
```

## 并发性能分析

### 真正的并发执行

FlowCoro 实现真正的多线程并发：

```cpp
Task<void> concurrent_demo() {
    // 三个任务在不同线程中并发执行
    auto task1 = async_compute(1);  // 线程 A
    auto task2 = async_compute(2);  // 线程 B  
    auto task3 = async_compute(3);  // 线程 C
    
    // 等待所有结果
    auto r1 = co_await task1;
    auto r2 = co_await task2;
    auto r3 = co_await task3;
    
    co_return;
}
```

### 性能特点

| 特性 | FlowCoro | 传统多线程 | 优势 |
|------|----------|------------|------|
| 任务创建 | 231ns | 50μs | 216倍 |
| 内存开销 | 407 bytes | 8MB | 20000倍 |
| 上下文切换 | 用户级 | 内核级 | 100倍 |
| 线程数量 | 固定池 | 动态创建 | 稳定 |

### 并发模型对比

#### FlowCoro 协程模型
```cpp
// 轻量级：每个任务只占用 400+ 字节
std::vector<Task<int>> tasks;
for (int i = 0; i < 10000; ++i) {
    tasks.push_back(compute_task(i));  // 总内存：4MB
}
```

#### 传统多线程模型
```cpp
// 重量级：每个线程占用 8MB 栈空间
std::vector<std::thread> threads;
for (int i = 0; i < 10000; ++i) {
    threads.emplace_back([i]() {       // 总内存：80GB
        compute_task(i);
    });
}
```

## 架构适用场景

###  强烈推荐的场景

**1. Web API 服务器**
```cpp
Task<Response> handle_request(const Request& req) {
    // 每个请求独立处理，完美匹配
    auto result = co_await database_query(req.id);
    co_return create_response(result);
}
```

**2. 批量数据处理**
```cpp
Task<void> batch_processing() {
    std::vector<Task<ProcessResult>> tasks;
    
    // 批量创建任务
    for (const auto& item : data_items) {
        tasks.push_back(process_item(item));
    }
    
    // 批量等待结果
    for (auto& task : tasks) {
        auto result = co_await task;
        save_result(result);
    }
}
```

**3. 爬虫和测试工具**
```cpp
Task<void> web_crawler() {
    std::vector<Task<WebPage>> tasks;
    
    for (const auto& url : urls) {
        tasks.push_back(fetch_page(url));
    }
    
    for (auto& task : tasks) {
        auto page = co_await task;
        process_page(page);
    }
}
```

**4. 生产者-消费者模式（通过Channel实现）**
```cpp
Task<void> channel_producer_consumer() {
    auto channel = make_channel<WorkItem>(10);
    
    auto producer_task = [channel]() -> Task<void> {
        for (int i = 0; i < 100; ++i) {
            WorkItem item = create_work_item(i);
            co_await channel->send(item);
        }
        channel->close();
    };
    
    auto consumer_task = [channel]() -> Task<void> {
        while (true) {
            auto item = co_await channel->recv();
            if (!item.has_value()) break;
            process_work_item(item.value());
        }
    };
    
    auto prod = producer_task();
    auto cons = consumer_task();
    
    co_await prod;
    co_await cons;
}
```

### ✅ 已解决的场景（通过Channel）

**1. 生产者-消费者模式**
```cpp
// ✓ 现在支持：使用Channel实现协程间通信
Task<void> producer_consumer() {
    auto channel = make_channel<Data>(20);
    
    auto producer_task = [channel]() -> Task<void> {
        for (int i = 0; i < 100; ++i) {
            co_await channel->send(Data{i});
        }
        channel->close();
    };
    
    auto consumer_task = [channel]() -> Task<void> {
        while (true) {
            auto data = co_await channel->recv();
            if (!data.has_value()) break;
            process_data(data.value());
        }
    };
    
    auto prod = producer_task();
    auto cons = consumer_task();
    
    co_await prod;
    co_await cons;
}
```

**2. 事件驱动处理**
```cpp
// ✓ 现在支持：基于Channel的事件系统
Task<void> event_driven_system() {
    auto event_channel = make_channel<Event>(50);
    
    // 事件监听协程
    auto listener = [event_channel]() -> Task<void> {
        while (true) {
            auto event = get_next_event(); // 从外部获取事件
            if (!event.has_value()) break;
            co_await event_channel->send(event.value());
        }
        event_channel->close();
    };
    
    // 事件处理协程
    auto processor = [event_channel]() -> Task<void> {
        while (true) {
            auto event = co_await event_channel->recv();
            if (!event.has_value()) break;
            handle_event(event.value());
        }
    };
    
    auto listen_task = listener();
    auto process_task = processor();
    
    co_await listen_task;
    co_await process_task;
}
```

**3. 流水线协作**
```cpp
// ✓ 现在支持：多阶段数据处理流水线
Task<void> data_pipeline() {
    auto raw_channel = make_channel<RawData>(10);
    auto processed_channel = make_channel<ProcessedData>(10);
    auto final_channel = make_channel<FinalData>(10);
    
    // 第一阶段：数据采集
    auto collector = [raw_channel]() -> Task<void> {
        for (int i = 0; i < 100; ++i) {
            RawData data = collect_raw_data(i);
            co_await raw_channel->send(data);
        }
        raw_channel->close();
    };
    
    // 第二阶段：数据处理
    auto processor = [raw_channel, processed_channel]() -> Task<void> {
        while (true) {
            auto raw = co_await raw_channel->recv();
            if (!raw.has_value()) break;
            
            ProcessedData processed = process_data(raw.value());
            co_await processed_channel->send(processed);
        }
        processed_channel->close();
    };
    
    // 第三阶段：数据输出
    auto outputer = [processed_channel, final_channel]() -> Task<void> {
        while (true) {
            auto processed = co_await processed_channel->recv();
            if (!processed.has_value()) break;
            
            FinalData final = finalize_data(processed.value());
            co_await final_channel->send(final);
        }
        final_channel->close();
    };
    
    // 启动流水线
    auto collect_task = collector();
    auto process_task = processor();
    auto output_task = outputer();
    
    co_await collect_task;
    co_await process_task;
    co_await output_task;
}
```

### ❌ 仍然不适合的场景

**极低延迟系统**: 微秒级延迟要求的高频交易等场景

## Channel 架构集成

Channel 功能与 FlowCoro 的三层调度架构完美集成：

### Channel 在调度系统中的位置

```text
应用协程 (Producer/Consumer Tasks)
    ↓ Channel.send() / Channel.recv()
Channel 缓冲区 (线程安全队列)
    ↓ 协程挂起/恢复机制
协程管理器 (等待队列管理)
    ↓ schedule_resume()
协程池 (多调度器处理)
    ↓ 无锁任务分发
线程池 (工作线程执行)
```

### Channel 协程调度机制

```cpp
// Channel send 操作的调度流程
Task<bool> Channel<T>::send(T value) {
    // 1. 检查缓冲区空间
    if (buffer_.size() < capacity_) {
        buffer_.push(std::move(value));
        notify_waiting_receivers(); // 唤醒等待的接收协程
        co_return true;
    }
    
    // 2. 缓冲区满，挂起发送协程
    co_await SenderAwaiter{this, std::move(value)};
    co_return true;
}

// Channel recv 操作的调度流程  
Task<std::optional<T>> Channel<T>::recv() {
    // 1. 检查缓冲区数据
    if (!buffer_.empty()) {
        T value = std::move(buffer_.front());
        buffer_.pop();
        notify_waiting_senders(); // 唤醒等待的发送协程
        co_return value;
    }
    
    // 2. 缓冲区空，挂起接收协程
    auto result = co_await ReceiverAwaiter{this};
    co_return result;
}
```

### Channel 性能特点

- **线程安全**: 所有操作都通过原子操作和锁保护
- **协程友好**: 使用协程挂起而非阻塞线程
- **缓冲优化**: 有界缓冲区避免内存无限增长
- **调度集成**: 完全集成到 FlowCoro 的调度系统中

## 性能调优指南

### 1. 批量大小优化

```cpp
// 推荐：分批处理大量任务
const int BATCH_SIZE = 100;

for (int start = 0; start < total_items; start += BATCH_SIZE) {
    std::vector<Task<Result>> batch_tasks;
    
    // 创建一批任务
    for (int i = start; i < std::min(start + BATCH_SIZE, total_items); ++i) {
        batch_tasks.push_back(process_item(i));
    }
    
    // 处理当前批次
    for (auto& task : batch_tasks) {
        auto result = co_await task;
        handle_result(result);
    }
}
```

### 2. 内存预分配

```cpp
// 推荐：预分配容器
std::vector<Task<int>> tasks;
tasks.reserve(task_count);  // 避免动态扩容

std::vector<int> results;
results.reserve(task_count);  // 预分配结果容器
```

### 3. 线程池配置

```cpp
// 自动配置（推荐）
// 调度器数量 = CPU 核心数
// 工作线程数 = CPU 核心数 * 2-4

// 手动配置（高级用法）
// 根据 I/O 密集程度调整线程数
```

## 监控和调试

### 性能监控

```cpp
// 获取协程池统计信息
auto stats = CoroutinePool::get_instance().get_stats();

std::cout << "调度器数量: " << stats.num_schedulers << std::endl;
std::cout << "待处理协程: " << stats.pending_coroutines << std::endl;
std::cout << "完成率: " << stats.completion_rate << std::endl;
```

### 调试技巧

```cpp
// 1. 检查任务状态
if (task.done()) {
    std::cout << "任务已完成" << std::endl;
}

// 2. 异常处理
try {
    auto result = co_await task;
} catch (const std::exception& e) {
    std::cout << "任务异常: " << e.what() << std::endl;
}

// 3. 超时处理（通过 sleep_for 实现）
auto timeout_task = []() -> Task<bool> {
    co_await sleep_for(std::chrono::seconds(5));
    co_return false;  // 超时
}();
```

## 总结

FlowCoro 的同步阻塞调度架构虽然限制了某些使用场景，但在批量任务处理方面表现卓越：

### 优势

- **极高性能**: 47万+ req/s 吞吐量（最新测试数据）
- **无锁架构**: 基于lock-free数据结构，避免线程竞争
- **智能调度**: 自适应负载均衡，最优化资源利用
- **内存高效**: 408 bytes/任务，包含丰富功能
- **线性扩展**: 从小规模到万级任务线性扩展

### 限制

- **批量处理导向**: 专为请求-响应模式优化
- **有限协程通信**: continuation支持有限的协程协作
- **内存开销**: 相比纯多线程有额外的协程和无锁队列开销

选择 FlowCoro 时，需要根据具体应用场景判断是否与架构特点匹配。
- **线程池并发**: 底层使用32-128个工作线程（根据CPU核心数调整）
- **IO操作并发**: 协程间的IO操作（如sleep_for）可以并行进行

### 2. 并发模型对比

| 特性 | 传统多线程 | FlowCoro协程 |
|------|------------|--------------|
| 线程创建成本 | 高（每个任务一个线程） | 低（复用线程池） |
| 内存开销 | 高（每线程8MB栈） | 低（协程栈KB级别） |
| 上下文切换 | 内核级，成本高 | 用户级，成本低 |
| 调度控制 | 操作系统控制 | 应用程序控制 |
| 资源竞争 | 需要大量锁同步 | 协程间天然隔离 |

### 3. 性能特点

#### 3.1 优势场景

- **IO密集型任务**: 协程在等待IO时释放线程，提高线程利用率
- **大量并发连接**: 可以轻松处理万级并发
- **延迟敏感应用**: 避免线程创建/销毁开销

#### 3.2 限制场景

- **CPU密集型任务**: 由于协程共享线程，CPU密集型任务可能阻塞其他协程
- **阻塞调用**: 传统的阻塞IO调用会阻塞整个线程

## 调度策略

### 1. 协程调度策略

```cpp
// 小任务: 使用when_all语法糖简化等待
if (request_count <= 3) {
    // 任务创建时已开始并发执行，when_all只是等待语法糖
    auto results = co_await when_all(task1, task2, task3);
}

// 大任务: 批量创建，自动并发执行
else {
    std::vector<Task<std::string>> all_tasks;
    for (int i = 0; i < request_count; ++i) {
        // 每个Task创建时立即开始并发执行
        all_tasks.push_back(handle_single_request(1000 + i));
    }
    // 等待所有已在并发执行的任务完成
    for (auto& task : all_tasks) {
        auto result = co_await task;
    }
}
```

### 2. 线程池配置

```cpp
// 动态线程数配置
size_t thread_count = std::thread::hardware_concurrency();
thread_count = std::max(thread_count, 32u);  // 最少32个线程
thread_count = std::min(thread_count, 128u); // 最多128个线程
```

## 生命周期管理

### 1. 协程生命周期状态

```cpp
enum class CoroutineState {
    created,    // 已创建，未开始执行
    running,    // 正在执行
    suspended,  // 挂起等待（如等待IO）
    completed,  // 执行完成
    cancelled,  // 已取消
    destroyed   // 已销毁
};
```

### 2. 资源清理策略

```cpp
// 安全销毁流程
void safe_destroy() {
    if (handle && handle.address()) {
        try {
            // 标记为已销毁
            handle.promise().mark_destroyed();
            // 安全销毁句柄
            handle.destroy();
        } catch (...) {
            // 忽略销毁异常
        }
        handle = nullptr;
    }
}
```

## 错误处理机制

### 1. 异常隔离

```cpp
// 协程执行异常不会影响线程池
thread_pool_->enqueue_void([handle, this]() {
    try {
        handle.resume();
        completed_coroutines_.fetch_add(1);
    } catch (...) {
        completed_coroutines_.fetch_add(1);
        std::cerr << "[FlowCoro] Coroutine execution exception in thread " 
                  << std::this_thread::get_id() << std::endl;
    }
});
```

### 2. 资源泄漏防护

```cpp
// RAII守卫确保状态正确清理
struct ExecutionGuard {
    ~ExecutionGuard() { 
        state->end_execution(); 
        state->reset_scheduled(); 
    }
};
```

## 性能调优指南

### 1. 线程池调优

```cpp
// CPU密集型应用: 线程数 = CPU核心数
size_t thread_count = std::thread::hardware_concurrency();

// IO密集型应用: 线程数 = CPU核心数 * 2-4
size_t thread_count = std::thread::hardware_concurrency() * 3;
```

### 2. 协程批处理优化

```cpp
// 批量处理减少锁竞争
const size_t BATCH_SIZE = 64;
std::vector<std::coroutine_handle<>> batch;
// 批量提取和执行协程
```

### 3. 内存优化

```cpp
// 预分配容器避免动态扩容
std::vector<Task<std::string>> all_tasks;
all_tasks.reserve(request_count);
```

## 使用示例

### 1. 基本协程任务

```cpp
Task<std::string> process_request(int user_id) {
    // 模拟异步IO操作
    co_await sleep_for(std::chrono::milliseconds(50));
    
    std::string result = "User " + std::to_string(user_id) + " processed";
    co_return result;
}
```

### 2. 并发任务处理

```cpp
Task<void> handle_concurrent_requests(int count) {
    std::vector<Task<std::string>> tasks;
    
    // 创建所有任务（它们会在线程池中并发执行）
    for (int i = 0; i < count; ++i) {
        tasks.push_back(process_request(i));
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        auto result = co_await task;
        std::cout << "Result: " << result << std::endl;
    }
}
```

### 3. 启动协程系统

```cpp
int main() {
    // 启用FlowCoro功能
    enable_v2_features();
    
    // 创建并执行主任务
    auto main_task = handle_concurrent_requests(1000);
    main_task.get();
    
    return 0;
}
```

## 与其他协程库对比

| 特性 | FlowCoro | cppcoro | libco |
|------|----------|---------|-------|
| 标准兼容 | C++20标准协程 | C++20标准协程 | 自定义实现 |
| 调度器 | 多线程线程池 | 单线程事件循环 | 手动切换 |
| 内存安全 | RAII + 原子操作 | 智能指针 | 手动管理 |
| 性能 | 中等（安全开销） | 高 | 高 |
| 易用性 | 高 | 中等 | 低 |

## 注意事项和限制

### 1. 跨线程访问限制

```cpp
// 错误: 协程句柄不应在不同线程间传递
std::thread t([handle]() {
    handle.resume(); // 可能导致段错误
});
```

### 2. 阻塞调用注意事项

```cpp
// 错误: 阻塞调用会阻塞整个工作线程
Task<void> bad_example() {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 阻塞线程
}

// 正确: 使用协程友好的休眠
Task<void> good_example() {
    co_await sleep_for(std::chrono::seconds(1)); // 非阻塞
}
```

### 3. 异常处理建议

```cpp
Task<int> safe_task() {
    try {
        // 可能抛出异常的操作
        co_return risky_operation();
    } catch (const std::exception& e) {
        std::cerr << "Task exception: " << e.what() << std::endl;
        co_return -1; // 返回错误码
    }
}
```

## 监控和调试

### 1. 协程池统计

```cpp
// 打印协程池统计信息
print_pool_stats();
```

### 2. 性能监控

```cpp
auto start = std::chrono::high_resolution_clock::now();
// 执行协程任务
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
```

## 总结

FlowCoro 的协程调度系统通过以下方式实现高效并发：

1. **真正的并发执行**: 协程在多线程环境中并发运行
2. **安全的资源管理**: 通过原子操作和RAII确保线程安全
3. **灵活的调度策略**: 支持从小规模到大规模的并发场景
4. **优化的性能**: 减少线程创建开销，提高资源利用率
5. **协程间通信**: 通过Channel实现安全的异步消息传递
6. **生产者-消费者支持**: 完整支持复杂的协程协作模式

### 核心优势

- **高性能调度**: 420万次/秒协程创建和执行
- **无锁队列**: 961万次/秒的高效任务分发
- **智能负载均衡**: 自适应调度器选择
- **Channel通信**: 线程安全的协程间数据传递
- **流水线处理**: 支持多阶段数据处理工作流

该系统既适合IO密集型和高并发场景，也能处理复杂的生产者-消费者模式和事件驱动应用，以较低的资源消耗实现大量并发任务处理。
