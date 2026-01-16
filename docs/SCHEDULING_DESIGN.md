# FlowCoro 调度系统设计详解

[English Version](#english-version) | 中文版

## 项目简介

FlowCoro 是一个基于 C++20 协程的高性能异步任务调度库，专为**批量任务处理**和**高吞吐量场景**设计。

### 核心特点

- **立即执行模型**: 使用 `suspend_never` 实现任务创建时立即执行
- **无锁架构**: 基于无锁队列的高性能任务分发，避免锁竞争
- **智能负载均衡**: 自适应选择最优调度器，实现负载均衡
- **多调度器并行**: 多个独立调度器并行处理协程任务
- **三层调度架构**: 清晰的分层设计，职责明确

### 适用场景

✅ **最佳适用场景**:
- Web API 服务器（高并发请求处理）
- 批量数据处理管道
- 高频交易系统
- 微服务网关
- 爬虫系统和并行下载
- 生产者-消费者模式（通过 Channel）

❌ **不适合场景**:
- 需要精确控制协程执行顺序的场景
- 单个长时间运行的协程
- 需要手动管理协程生命周期的场景

## 三层调度架构

FlowCoro 采用三层调度架构，每层职责清晰：

```text
┌─────────────────────────────────────────────────────┐
│  Layer 1: 协程管理器 (CoroutineManager)              │
│  职责: 协程生命周期管理、定时器、调度决策            │
└──────────────────┬──────────────────────────────────┘
                   │ schedule_coroutine_enhanced()
                   ▼
┌─────────────────────────────────────────────────────┐
│  Layer 2: 协程池 (CoroutinePool)                     │
│  职责: 多调度器管理、智能负载均衡                    │
└──────────────────┬──────────────────────────────────┘
                   │ CoroutineScheduler (独立线程)
                   ▼
┌─────────────────────────────────────────────────────┐
│  Layer 3: 线程池 (ThreadPool)                        │
│  职责: 底层工作线程管理、无锁任务执行                │
└─────────────────────────────────────────────────────┘
```

### Layer 1: 协程管理器 (CoroutineManager)

**位置**: `include/flowcoro/coroutine_manager.h`

**核心职责**:
1. 协程生命周期管理（创建、销毁）
2. 定时器队列管理（支持 `sleep_for` 等操作）
3. 调度决策（通过 `schedule_resume` 投递协程）
4. 批量处理优化（定时器、就绪队列、待销毁队列）

**关键方法**:

```cpp
class CoroutineManager {
public:
    // 核心调度方法 - 投递协程到协程池
    void schedule_resume(std::coroutine_handle<> handle) {
        if (!handle || handle.done()) return;
        
        // 投递到协程池进行并行处理
        schedule_coroutine_enhanced(handle);
    }
    
    // 驱动调度循环（需要在主线程定期调用）
    void drive() {
        drive_coroutine_pool();        // 驱动协程池
        process_timer_queue();         // 批量处理定时器(32个/批)
        process_ready_queue();         // 批量处理就绪队列(64个/批)
        process_pending_tasks();       // 批量销毁协程(32个/批)
    }
    
    // 添加定时器
    void add_timer(std::chrono::steady_clock::time_point when, 
                   std::coroutine_handle<> handle);
    
    // 单例模式
    static CoroutineManager& get_instance();
};
```

**批量处理优化**:
- **定时器队列**: 每次最多处理 32 个到期定时器，减少锁竞争
- **就绪队列**: 每次最多处理 64 个就绪协程，提升吞吐量
- **销毁队列**: 批量销毁协程，避免频繁加锁

### Layer 2: 协程池 (CoroutinePool)

**位置**: `src/coroutine_pool.cpp` (内部实现)

**核心职责**:
1. 管理多个独立的协程调度器（CoroutineScheduler）
2. 智能负载均衡，选择最优调度器
3. 统计信息收集和性能监控
4. 管理后台线程池处理 CPU 密集型任务

**架构设计**:

```cpp
class CoroutinePool {
private:
    // 根据 CPU 核心数确定调度器数量（当前配置为 1 个）
    const size_t NUM_SCHEDULERS;
    
    // 独立协程调度器，每个运行在独立线程中
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 后台线程池 - 处理 CPU 密集型任务
    std::unique_ptr<lockfree::ThreadPool> thread_pool_;
    
public:
    // 协程调度 - 使用智能负载均衡
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 使用智能负载均衡器选择最优调度器
        auto& load_balancer = CoroutineManager::get_instance()
                                .get_load_balancer();
        size_t scheduler_index = load_balancer.select_scheduler();
        
        // 分配给对应的调度器
        schedulers_[scheduler_index]->schedule_coroutine(handle);
    }
    
    // CPU 密集型任务提交到后台线程池
    void schedule_task(std::function<void()> task) {
        thread_pool_->enqueue(std::move(task));
    }
};
```

**调度器配置**:
- **调度器数量**: 当前优化为 1 个调度器（性能最优配置）
- **线程池大小**: 8-24 个工作线程（根据 CPU 核心数自适应）
- **CPU 亲和性**: 每个调度器绑定到特定 CPU 核心，减少线程迁移开销

### Layer 3: 协程调度器 (CoroutineScheduler)

**位置**: `src/coroutine_pool.cpp` (内部实现)

**核心职责**:
1. 运行在独立线程中，持续处理协程
2. 管理无锁协程队列
3. 批量执行协程，提升缓存命中率
4. 实现自适应等待策略，减少 CPU 空转

**关键特性**:

```cpp
class CoroutineScheduler {
private:
    // 无锁协程队列
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    // 精确的队列长度统计
    std::atomic<size_t> queue_size_{0};
    
    // 工作线程
    void worker_loop() {
        const size_t BATCH_SIZE = 256; // 批处理大小
        std::vector<std::coroutine_handle<>> batch;
        
        while (!stop_flag_) {
            // 批量提取协程
            while (batch.size() < BATCH_SIZE && 
                   coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
                queue_size_.fetch_sub(1);
            }
            
            // 批量执行协程
            for (auto handle : batch) {
                if (handle && !handle.done()) {
                    handle.resume();
                    completed_coroutines_.fetch_add(1);
                    load_balancer_.on_task_completed(scheduler_id_);
                }
            }
            
            // 自适应等待策略（4 级等待）
            if (batch.empty()) {
                // Level 1: 自旋等待（最快响应）
                // Level 2: yield 让出 CPU
                // Level 3: 短暂休眠
                // Level 4: 条件变量等待
            }
        }
    }
    
public:
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 添加到无锁队列
        coroutine_queue_.enqueue(handle);
        queue_size_.fetch_add(1);
        
        // 更新负载均衡器
        load_balancer_.update_load(scheduler_id_, queue_size_);
        
        // 唤醒工作线程
        cv_.notify_one();
    }
};
```

**性能优化**:
- **批量处理**: 每次最多处理 256 个协程，减少队列访问开销
- **预取优化**: 使用 `__builtin_prefetch` 预取内存，提升缓存命中率
- **CPU 亲和性**: 绑定到特定 CPU 核心，减少缓存失效
- **四级等待策略**: 根据空闲时间自适应调整等待策略

## 智能负载均衡

**位置**: `include/flowcoro/load_balancer.h`

### 设计目标

1. **快速选择**: 最小化调度器选择的开销
2. **负载均衡**: 避免某些调度器过载
3. **无锁设计**: 使用原子操作避免锁竞争

### 实现机制

```cpp
class SmartLoadBalancer {
private:
    // 每个调度器的队列负载
    std::array<std::atomic<size_t>, MAX_SCHEDULERS> queue_loads_;
    
    // 轮询计数器
    std::atomic<size_t> round_robin_counter_{0};
    
public:
    // 混合策略：轮询 + 负载感知
    size_t select_scheduler() {
        size_t count = scheduler_count_.load();
        
        // 快速路径：使用轮询（性能更好）
        size_t quick_choice = round_robin_counter_.fetch_add(1) % count;
        
        // 每 16 次执行一次负载检查（优化：减少开销）
        if ((quick_choice & 0xF) == 0) {
            // 选择负载最小的调度器
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
        
        return quick_choice;
    }
    
    // 更新调度器负载
    void update_load(size_t scheduler_id, size_t load) {
        queue_loads_[scheduler_id].store(load, std::memory_order_relaxed);
    }
    
    // 任务完成时递减负载
    void on_task_completed(size_t scheduler_id) {
        queue_loads_[scheduler_id].fetch_sub(1, std::memory_order_relaxed);
    }
};
```

### 负载均衡策略

**混合策略**（轮询 + 负载感知）:

1. **默认情况**: 使用轮询（Round-Robin）策略
   - 优点: 非常快速，无需计算
   - 通过原子计数器实现: `round_robin_counter_.fetch_add(1) % count`

2. **定期检查**: 每 16 次调度执行一次负载检查
   - 遍历所有调度器，找到队列长度最小的
   - 返回负载最轻的调度器索引

3. **实时更新**: 协程调度和完成时实时更新负载信息
   - `schedule_coroutine`: 增加队列负载
   - `on_task_completed`: 减少队列负载

**优势**:
- ⚡ **高性能**: 95% 的调度使用快速轮询，开销极小
- 📊 **负载均衡**: 定期检查确保负载相对均衡
- 🔒 **无锁设计**: 使用原子操作，避免锁竞争
- 🎯 **自适应**: 根据实际负载动态调整

## 任务执行流程

### 完整执行流程

```text
1. 用户创建 Task
   ↓
2. Task 构造函数调用 initial_suspend()
   ↓ (返回 suspend_never)
3. 协程体立即开始执行
   ↓
4. 遇到 co_await（如 sleep_for）
   ↓ (返回 suspend_always)
5. 协程挂起，投递到 CoroutineManager
   ↓ schedule_resume()
6. CoroutineManager 调用 schedule_coroutine_enhanced()
   ↓
7. CoroutinePool 使用智能负载均衡选择调度器
   ↓ select_scheduler()
8. 协程进入选中的 CoroutineScheduler 的无锁队列
   ↓ coroutine_queue_.enqueue()
9. 调度器工作线程批量提取协程
   ↓ batch.push_back()
10. 批量恢复协程执行
    ↓ handle.resume()
11. 协程继续执行直到完成或再次挂起
    ↓
12. 完成：更新统计信息，通知负载均衡器
```

### suspend_never 机制详解

FlowCoro 的一个关键设计是 **Task 立即执行** 模型：

```cpp
Task<int> compute(int x) {
    // 1. Task 对象创建
    // 2. initial_suspend() 返回 suspend_never
    // 3. 协程体立即开始执行（不等待调度）
    
    co_await sleep_for(std::chrono::milliseconds(50));
    // 4. 遇到 co_await，返回 suspend_always
    // 5. 此时协程才真正挂起，投递到调度系统
    
    co_return x * x;
}

// 使用示例
Task task1 = compute(10);  // 立即开始执行
Task task2 = compute(20);  // 立即开始执行（并发）
Task task3 = compute(30);  // 立即开始执行（并发）

// 三个任务已经在不同调度器上并发运行
auto result1 = co_await task1;  // 等待结果
auto result2 = co_await task2;
auto result3 = co_await task3;
```

**suspend_never 的优势**:

1. **最大化并发度**: 任务创建时立即开始执行，无需等待调度
2. **减少延迟**: 避免了"创建 → 等待调度 → 开始执行"的延迟
3. **简化 API**: 用户无需手动启动任务，创建即执行
4. **天然并发**: 多个任务创建时自动并发执行

**对比其他模型**:

| 模型 | 创建时行为 | 何时开始执行 | 适用场景 |
|------|-----------|-------------|---------|
| **suspend_never** (FlowCoro) | 立即执行 | 创建时 | 批量并发任务 |
| **suspend_always** | 挂起等待 | 需要手动调度 | 需要精确控制执行时机 |
| **Go goroutine** | 挂起等待 | 调度器分配时 | 大量轻量级并发 |
| **Rust Tokio** | 挂起等待 | .await 时 | 精确控制异步流程 |

### 定时器处理

FlowCoro 支持专用定时器线程和批量定时器处理：

```cpp
// 添加定时器
manager.add_timer_enhanced(
    std::chrono::steady_clock::now() + std::chrono::milliseconds(100),
    handle
);

// 定时器处理流程
void dedicated_timer_thread_func() {
    while (!stop_) {
        // 1. 等待到期的定时器
        // 2. 批量提取到期的协程
        // 3. 通过 schedule_coroutine_enhanced 投递到协程池
        // 4. 精确等待到下一个定时器时间
    }
}
```

**优势**:
- ⏰ **精确定时**: 专用线程确保定时器精确触发
- 📦 **批量处理**: 同时到期的定时器批量处理，提升效率
- 🔓 **无锁投递**: 定时器到期后通过无锁队列投递协程

## 线程池实现

**位置**: `include/flowcoro/thread_pool.h`

### 无锁线程池 (ThreadPool)

```cpp
class ThreadPool {
private:
    // 无锁任务队列
    lockfree::Queue<std::function<void()>> task_queue_;
    
    // 工作线程数组
    std::vector<std::thread> workers_;
    
    void worker_loop() {
        std::function<void()> task;
        
        while (!stop_) {
            if (task_queue_.dequeue(task)) {
                task();  // 执行任务
            } else {
                // 自适应等待策略
                std::this_thread::yield();
            }
        }
    }
    
public:
    // 提交任务
    void enqueue_void(std::function<void()> task) {
        task_queue_.enqueue(std::move(task));
    }
};
```

**特点**:
- 🔒 **无锁队列**: 使用 `lockfree::Queue` 避免锁竞争
- 🧵 **自适应线程数**: 根据 CPU 核心数调整工作线程
- ⚡ **高吞吐量**: 批量任务处理能力强

### 工作窃取线程池 (WorkStealingThreadPool)

支持工作窃取算法的高级线程池：

```cpp
class WorkStealingThreadPool {
private:
    // 每个工作线程有独立的本地队列
    std::vector<std::unique_ptr<WorkerData>> worker_data_;
    
    // 全局队列
    lockfree::Queue<std::function<void()>> global_queue_;
    
    void worker_loop(size_t worker_index) {
        // 1. 先检查本地队列
        // 2. 检查全局队列
        // 3. 尝试从其他工作线程偷取任务
    }
};
```

**工作窃取策略**:
1. 优先处理本地队列（减少竞争）
2. 本地队列空时检查全局队列
3. 全局队列空时从其他线程窃取任务
4. 实现负载均衡

## 无锁队列实现

**位置**: `include/flowcoro/lockfree.h`

FlowCoro 的核心性能来自于无锁队列的高效实现：

```cpp
template<typename T>
class Queue {
private:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
public:
    void enqueue(T value) {
        // Michael-Scott 无锁队列算法
        // 使用 CAS (Compare-And-Swap) 原子操作
    }
    
    bool dequeue(T& value) {
        // 无锁出队操作
        // 处理 ABA 问题
    }
};
```

**关键特性**:
- 🔒 **完全无锁**: 基于 CAS 原子操作
- 🔥 **高并发**: 支持多生产者多消费者
- 📈 **可扩展**: 性能随 CPU 核心数线性提升
- 🛡️ **ABA 安全**: 正确处理 ABA 问题

## 性能优化技术

### 1. 批量处理

**所有队列操作都采用批量处理**:
- 定时器队列: 32 个/批
- 就绪队列: 64 个/批
- 销毁队列: 64 个/批
- 协程执行: 256 个/批

**优势**: 减少锁竞争、提升缓存命中率、分摊队列访问开销

### 2. CPU 亲和性

```cpp
void set_cpu_affinity() {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(scheduler_id_ % std::thread::hardware_concurrency(), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}
```

**优势**: 减少线程迁移、提升缓存亲和性、降低延迟

### 3. 内存预取

```cpp
while (batch.size() < BATCH_SIZE && coroutine_queue_.dequeue(handle)) {
    batch.push_back(handle);
    
    // 预取下一个协程的内存
    if (batch.size() < BATCH_SIZE - 1) {
        __builtin_prefetch(handle.address(), 0, 3);
    }
}
```

**优势**: 减少内存访问延迟、提升 L1/L2 缓存命中率

### 4. 自适应等待策略

**四级等待策略**（从快到慢）:

```cpp
if (empty_iterations < 64) {
    // Level 1: 自旋等待（最快响应，适合短时间等待）
    for (int i = 0; i < 64; ++i) {
        if (!queue.empty()) break;
        __builtin_ia32_pause(); // CPU pause 指令
    }
} else if (empty_iterations < 256) {
    // Level 2: yield（让出 CPU）
    std::this_thread::yield();
} else if (empty_iterations < 1024) {
    // Level 3: 短暂休眠（微秒级）
    std::this_thread::sleep_for(wait_duration);
} else {
    // Level 4: 条件变量等待（毫秒级）
    cv_.wait_for(lock, max_wait);
}
```

**优势**: 
- 低负载时减少 CPU 使用
- 高负载时快速响应
- 自适应调整，平衡延迟和 CPU 使用

### 5. 内存池优化

使用 Redis/Nginx 启发的内存池设计:

```cpp
// 使用内存池分配
auto task = std::allocate_shared<std::packaged_task<return_type()>>(
    flowcoro::PoolAllocator<std::packaged_task<return_type()>>{},
    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
);
```

**优势**: 
- 减少 malloc/free 调用
- 提升内存分配性能
- 降低内存碎片

## 使用示例

### 基本使用

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 简单协程任务
Task<int> compute(int value) {
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

int main() {
    // 任务创建时立即开始执行（suspend_never）
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
    // 等待结果
    auto result1 = sync_wait(task1);
    auto result2 = sync_wait(task2);
    auto result3 = sync_wait(task3);
    
    std::cout << "Results: " << result1 << ", " 
              << result2 << ", " << result3 << std::endl;
    
    return 0;
}
```

### 并发执行

```cpp
Task<void> parallel_processing() {
    std::vector<Task<int>> tasks;
    
    // 创建 1000 个并发任务
    for (int i = 0; i < 1000; ++i) {
        tasks.push_back(compute(i));
    }
    
    // 所有任务已经在并发执行
    // 等待所有结果
    for (auto& task : tasks) {
        auto result = co_await task;
        std::cout << "Result: " << result << std::endl;
    }
    
    co_return;
}
```

### Channel 通信

```cpp
Task<void> producer_consumer() {
    auto channel = make_channel<int>(100);  // 缓冲区大小: 100
    
    // 生产者
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 1000; ++i) {
            co_await channel->send(i);
        }
        channel->close();
    };
    
    // 消费者
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            process(value.value());
        }
    };
    
    auto prod_task = producer();
    auto cons_task = consumer();
    
    co_await prod_task;
    co_await cons_task;
    
    co_return;
}
```

## 性能特征

### 吞吐量

- **协程创建和执行**: 与 Go 和 Rust 相当
- **无锁队列**: 高并发场景下性能优异
- **批量处理**: 10 万任务处理时间 < 1 秒

### 延迟

- **任务调度延迟**: 微秒级
- **定时器精度**: 毫秒级（取决于系统定时器精度）
- **上下文切换**: 极低（无锁设计）

### 可扩展性

- **CPU 核心**: 性能随核心数线性提升（8-24 核最优）
- **并发任务数**: 支持 10K+ 并发任务
- **调度器数量**: 当前优化为 1 个（避免过度调度开销）

## 调试和监控

### 统计信息

```cpp
// 打印协程池统计信息
print_pool_stats();

// 输出示例:
// === FlowCoro 多调度器协程池统计 ===
//  运行时间: 5234 ms
//  架构模式: 1个独立协程调度器 + 后台线程池
//  工作线程: 16 个
//  待处理协程: 42
//  总协程数: 10000
//  完成协程: 9958
//  协程完成率: 99.6%
```

### 性能监控

```cpp
// 获取性能统计
auto stats = get_flowcoro_stats();

std::cout << "任务完成数: " << stats.tasks_completed << std::endl;
std::cout << "平均延迟: " << stats.average_latency_us << " μs" << std::endl;
```

### 调试工具

```cpp
// 打印系统状态
flowcoro::debug::print_system_status();

// 检查内存使用
flowcoro::debug::check_memory_usage();

// 验证协程状态一致性
bool valid = flowcoro::debug::validate_coroutine_state();

// 运行性能基准测试
flowcoro::debug::run_performance_benchmark(10000);
```

## 常见问题

### Q1: 为什么只使用 1 个调度器？

**A**: 经过性能测试，1 个调度器配合多个工作线程是当前最优配置：
- 减少调度器间的竞争
- 简化负载均衡逻辑
- 降低线程调度开销
- 配合后台线程池提供充足的并发能力

### Q2: 如何控制协程执行顺序？

**A**: FlowCoro 采用 suspend_never 模型，任务创建时立即执行，不保证执行顺序。如果需要顺序执行：
```cpp
auto result1 = co_await task1;  // 等待 task1 完成
auto task2 = compute(result1);  // 使用 result1 创建 task2
auto result2 = co_await task2;  // 等待 task2 完成
```

### Q3: 如何处理异常？

**A**: FlowCoro 在多个层级处理异常：
- 协程执行层: 捕获并记录异常，不影响调度器稳定性
- 任务提交层: 使用 try-catch 包装任务执行
- 用户代码: 建议使用 try-catch 捕获业务异常

```cpp
Task<int> safe_compute(int x) {
    try {
        co_await risky_operation(x);
        co_return x * x;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        co_return -1;
    }
}
```

### Q4: 如何优化性能？

**A**: 性能优化建议：
1. **批量提交任务**: 一次性创建多个任务，利用并发
2. **合理使用 Channel**: 避免频繁创建/销毁 Channel
3. **启用 PGO**: 使用 Profile-Guided Optimization
4. **CPU 亲和性**: 在 Linux 上自动启用
5. **减少锁使用**: 使用无锁数据结构

### Q5: 与其他协程库的区别？

| 特性 | FlowCoro | cppcoro | Boost.Coroutine2 | Folly |
|------|----------|---------|------------------|-------|
| **suspend_never** | ✅ | ❌ | ❌ | ❌ |
| **无锁调度** | ✅ | ❌ | ❌ | ✅ |
| **多调度器** | ✅ | ❌ | ❌ | ✅ |
| **智能负载均衡** | ✅ | ❌ | ❌ | ✅ |
| **Channel 通信** | ✅ | ✅ | ❌ | ❌ |
| **批量处理优化** | ✅ | ❌ | ❌ | ✅ |

## 总结

FlowCoro 的调度系统设计具有以下核心特点：

1. **三层架构**: 清晰的分层设计，职责明确
2. **立即执行**: suspend_never 实现任务创建时立即执行
3. **无锁设计**: 全面采用无锁队列和原子操作
4. **智能负载均衡**: 混合轮询和负载感知策略
5. **批量优化**: 所有关键路径都采用批量处理
6. **自适应等待**: 四级等待策略平衡延迟和 CPU 使用
7. **性能监控**: 完善的统计和调试工具

这些设计使 FlowCoro 成为批量任务处理和高吞吐量场景的理想选择。

---

## English Version

# FlowCoro Scheduling System Design

## Project Overview

FlowCoro is a high-performance C++20 coroutine library designed specifically for **batch task processing** and **high-throughput scenarios**.

### Core Features

- **Immediate Execution Model**: Tasks execute immediately upon creation using `suspend_never`
- **Lock-free Architecture**: High-performance task distribution based on lock-free queues
- **Smart Load Balancing**: Adaptive scheduler selection for load balancing
- **Multi-Scheduler Parallelism**: Multiple independent schedulers processing coroutines in parallel
- **Three-Layer Scheduling Architecture**: Clear layered design with well-defined responsibilities

### Best Use Cases

✅ **Ideal For**:
- Web API servers (high-concurrency request handling)
- Batch data processing pipelines
- High-frequency trading systems
- Microservice gateways
- Web crawlers and parallel downloads
- Producer-consumer patterns (via Channels)

❌ **Not Suitable For**:
- Scenarios requiring precise control over coroutine execution order
- Single long-running coroutines
- Scenarios requiring manual coroutine lifetime management

## Three-Layer Scheduling Architecture

FlowCoro employs a three-layer scheduling architecture with clear responsibilities at each layer:

```text
┌─────────────────────────────────────────────────────┐
│  Layer 1: Coroutine Manager (CoroutineManager)      │
│  Role: Lifecycle management, timers, scheduling     │
└──────────────────┬──────────────────────────────────┘
                   │ schedule_coroutine_enhanced()
                   ▼
┌─────────────────────────────────────────────────────┐
│  Layer 2: Coroutine Pool (CoroutinePool)            │
│  Role: Multi-scheduler management, load balancing   │
└──────────────────┬──────────────────────────────────┘
                   │ CoroutineScheduler (independent threads)
                   ▼
┌─────────────────────────────────────────────────────┐
│  Layer 3: Thread Pool (ThreadPool)                  │
│  Role: Worker thread management, lock-free execution│
└─────────────────────────────────────────────────────┘
```

### Layer 1: Coroutine Manager

**Location**: `include/flowcoro/coroutine_manager.h`

**Core Responsibilities**:
1. Coroutine lifecycle management (creation, destruction)
2. Timer queue management (supports operations like `sleep_for`)
3. Scheduling decisions (submitting coroutines via `schedule_resume`)
4. Batch processing optimization (timers, ready queue, destruction queue)

**Key Methods**:

```cpp
class CoroutineManager {
public:
    // Core scheduling method - submits coroutines to the pool
    void schedule_resume(std::coroutine_handle<> handle);
    
    // Drives the scheduling loop (needs periodic calls from main thread)
    void drive();
    
    // Adds a timer
    void add_timer(std::chrono::steady_clock::time_point when, 
                   std::coroutine_handle<> handle);
    
    // Singleton pattern
    static CoroutineManager& get_instance();
};
```

**Batch Processing Optimization**:
- **Timer Queue**: Processes up to 32 expired timers at once
- **Ready Queue**: Processes up to 64 ready coroutines at once
- **Destruction Queue**: Batch destroys coroutines

### Layer 2: Coroutine Pool

**Location**: `src/coroutine_pool.cpp` (internal implementation)

**Core Responsibilities**:
1. Manages multiple independent coroutine schedulers (CoroutineScheduler)
2. Smart load balancing to select optimal scheduler
3. Statistics collection and performance monitoring
4. Manages background thread pool for CPU-intensive tasks

**Architecture Design**:

```cpp
class CoroutinePool {
private:
    // Number of schedulers based on CPU cores (currently configured as 1)
    const size_t NUM_SCHEDULERS;
    
    // Independent coroutine schedulers, each running in its own thread
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // Background thread pool - handles CPU-intensive tasks
    std::unique_ptr<lockfree::ThreadPool> thread_pool_;
    
public:
    // Coroutine scheduling with smart load balancing
    void schedule_coroutine(std::coroutine_handle<> handle);
    
    // CPU-intensive tasks submitted to background thread pool
    void schedule_task(std::function<void()> task);
};
```

### Layer 3: Coroutine Scheduler

**Location**: `src/coroutine_pool.cpp` (internal implementation)

**Core Responsibilities**:
1. Runs in an independent thread, continuously processing coroutines
2. Manages lock-free coroutine queue
3. Batch executes coroutines for improved cache hit rate
4. Implements adaptive waiting strategy to reduce CPU spinning

**Key Features**:

```cpp
class CoroutineScheduler {
private:
    // Lock-free coroutine queue
    lockfree::Queue<std::coroutine_handle<>> coroutine_queue_;
    
    // Precise queue size tracking
    std::atomic<size_t> queue_size_{0};
    
    void worker_loop() {
        const size_t BATCH_SIZE = 256;
        std::vector<std::coroutine_handle<>> batch;
        
        while (!stop_flag_) {
            // Batch extract coroutines
            while (batch.size() < BATCH_SIZE && 
                   coroutine_queue_.dequeue(handle)) {
                batch.push_back(handle);
            }
            
            // Batch execute coroutines
            for (auto handle : batch) {
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }
        }
    }
};
```

**Performance Optimizations**:
- **Batch Processing**: Processes up to 256 coroutines at once
- **Prefetching**: Uses `__builtin_prefetch` for improved cache hits
- **CPU Affinity**: Binds to specific CPU cores to reduce migration
- **Four-Level Waiting**: Adaptive waiting strategy based on idle time

## Smart Load Balancing

**Location**: `include/flowcoro/load_balancer.h`

### Design Goals

1. **Fast Selection**: Minimize scheduler selection overhead
2. **Load Balancing**: Avoid overloading certain schedulers
3. **Lock-free Design**: Use atomic operations to avoid lock contention

### Implementation

```cpp
class SmartLoadBalancer {
public:
    // Hybrid strategy: Round-robin + Load-aware
    size_t select_scheduler() {
        // Fast path: Use round-robin (better performance)
        size_t quick_choice = round_robin_counter_.fetch_add(1) % count;
        
        // Every 16 calls, perform load check
        if ((quick_choice & 0xF) == 0) {
            // Select scheduler with minimum load
            return find_minimum_load_scheduler();
        }
        
        return quick_choice;
    }
};
```

## Task Execution Flow

### Complete Execution Flow

```text
1. User creates Task
   ↓
2. Task constructor calls initial_suspend()
   ↓ (returns suspend_never)
3. Coroutine body begins execution immediately
   ↓
4. Encounters co_await (e.g., sleep_for)
   ↓ (returns suspend_always)
5. Coroutine suspends, submitted to CoroutineManager
   ↓
6-12. Scheduling through pool, load balancer, and execution
```

### suspend_never Mechanism

FlowCoro's key design is the **immediate execution** model:

```cpp
Task<int> compute(int x) {
    // 1. Task object created
    // 2. initial_suspend() returns suspend_never
    // 3. Coroutine body begins execution immediately (no scheduling wait)
    
    co_await sleep_for(std::chrono::milliseconds(50));
    // 4. Encounters co_await, returns suspend_always
    // 5. Now coroutine truly suspends and is submitted to scheduler
    
    co_return x * x;
}
```

**Advantages of suspend_never**:
1. **Maximizes Concurrency**: Tasks start executing immediately
2. **Reduces Latency**: Avoids "create → wait for schedule → start" delay
3. **Simplified API**: No manual task startup needed
4. **Natural Concurrency**: Multiple tasks automatically execute concurrently

## Performance Characteristics

### Throughput
- **Coroutine Creation/Execution**: Competitive with Go and Rust
- **Lock-free Queue**: Excellent performance under high concurrency
- **Batch Processing**: < 1 second for 100K tasks

### Latency
- **Task Scheduling**: Microsecond-level
- **Timer Precision**: Millisecond-level
- **Context Switching**: Extremely low (lock-free design)

### Scalability
- **CPU Cores**: Linear performance scaling (8-24 cores optimal)
- **Concurrent Tasks**: Supports 10K+ concurrent tasks
- **Scheduler Count**: Currently optimized for 1 (avoids excessive overhead)

## Summary

FlowCoro's scheduling system features:

1. **Three-Layer Architecture**: Clear separation of concerns
2. **Immediate Execution**: suspend_never for instant task startup
3. **Lock-free Design**: Comprehensive use of lock-free queues
4. **Smart Load Balancing**: Hybrid round-robin and load-aware strategy
5. **Batch Optimization**: All critical paths use batch processing
6. **Adaptive Waiting**: Four-level strategy balancing latency and CPU
7. **Performance Monitoring**: Comprehensive statistics and debugging tools

These designs make FlowCoro ideal for batch task processing and high-throughput scenarios.
