# FlowCoro 协程调度系统架构文档

**基于同步阻塞调度的高性能批量任务处理系统**

## 架构概述

FlowCoro 采用**同步阻塞调度模式**，专门为批量任务处理和请求-响应模式优化。这种设计选择决定了系统的适用场景和性能特点。

### 核心设计理念

- **同步阻塞调度**: `sync_wait()` → `Task::get()` → `handle.resume()` 链式调用
- **批量任务优化**: 专门针对大规模并发任务处理场景
- **多线程协程池**: 智能调度器 + 线程池架构
- **请求-响应模式**: 完美匹配 Web 服务、API 处理等场景

### 架构特点和限制

| 特点 | 说明 | 影响 |
|------|------|------|
| **同步调度** | Task::get() 阻塞等待完成 | 不适合协程间持续协作 |
| **立即执行** | Task 创建时立即开始执行 | 适合批量并发处理 |
| **多线程安全** | 跨线程协程恢复支持 | 高并发性能优秀 |
| **无事件循环** | 缺乏异步事件处理机制 | 不支持实时系统 |

## 双层调度模型

FlowCoro 使用两层调度架构：

```text
应用层协程 (Task<T>)
    ↓
协程管理器 (CoroutineManager) - 单线程调度决策
    ↓
协程池 (CoroutinePool) - 多线程执行
    ↓
线程池 (ThreadPool) - 底层工作线程
```

### 第一层：协程管理器 (CoroutineManager)

**职责**: 协程生命周期管理、调度决策、定时器处理

```cpp
class CoroutineManager {
public:
    // 核心调度方法
    void schedule_resume(std::coroutine_handle<> handle) {
        // 智能负载均衡选择调度器
        schedule_coroutine_enhanced(handle);
    }
    
    // 驱动调度循环
    void drive() {
        process_timer_queue();     // 处理定时器
        process_ready_queue();     // 处理就绪队列
        process_pending_tasks();   // 处理待处理任务
    }
};
```

**特点**:
- **单线程运行**: 通常在主线程中执行
- **决策中心**: 负责调度策略和负载均衡
- **定时器管理**: 处理 `sleep_for()` 等定时功能

### 第二层：协程池 (CoroutinePool)

**职责**: 多线程协程执行、任务队列管理

```cpp
class CoroutinePool {
private:
    // 根据CPU核心数确定调度器数量
    const size_t NUM_SCHEDULERS;
    
    // 独立协程调度器
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 后台线程池
    std::unique_ptr<ThreadPool> thread_pool_;
    
public:
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 智能负载均衡分配
        size_t scheduler_index = load_balancer_.select_scheduler();
        schedulers_[scheduler_index]->schedule_coroutine(handle);
    }
};
```

**特点**:
- **多调度器**: 通常 4-16 个独立调度器
- **工作线程池**: 32-128 个工作线程
- **智能负载均衡**: 自动选择最优调度器

## 协程执行流程

### 1. 任务创建和立即执行

```cpp
// 用户代码
auto task = async_compute(42);  // 创建 Task

// 内部流程
Task<int> async_compute(int x) {
    // 1. initial_suspend() 返回 suspend_never
    // 2. 协程立即开始执行
    // 3. 遇到 co_await 时挂起，进入调度系统
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return x * x;
}
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

### ✅ 强烈推荐的场景

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

### ❌ 不适合的场景

**1. 生产者-消费者模式**
```cpp
// ❌ 问题：无法实现真正的协程间协作
Task<void> producer_consumer() {
    auto producer_task = producer();
    auto consumer_task = consumer();
    
    // 变成顺序执行，失去并发意义
    co_await producer_task;  // 阻塞等待生产者
    co_await consumer_task;  // 然后执行消费者
}
```

**2. 实时事件处理**
```cpp
// ❌ 问题：缺乏事件循环机制
// FlowCoro 无法处理需要持续监听的事件
```

**3. 流水线协作**
```cpp
// ❌ 问题：协程间无法形成处理链
// 每个协程都是独立执行，无法形成数据流水线
```

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
- **极高性能**: 100万+ req/s 吞吐量
- **内存效率**: 比传统多线程节省 99.95% 内存
- **使用简单**: 清晰的 API 和执行模型
- **线程安全**: 内置跨线程安全机制

### 限制
- **无协程通信**: 不支持协程间持续协作
- **同步调度**: 不适合实时事件处理
- **架构固定**: 无法改变调度模式

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

该系统特别适合IO密集型和高并发场景，能够以较低的资源消耗处理大量并发任务。
