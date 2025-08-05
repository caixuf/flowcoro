# FlowCoro 协程调度系统详细文档

## 概述

FlowCoro 采用混合式协程调度架构，结合了单线程协程管理器和多线程线程池，旨在提供既安全又高效的协程执行环境。

## 核心架构

### 1. 双层调度模型

FlowCoro 使用两层调度模型：

```text
应用层协程
    ↓
协程管理器 (CoroutineManager) - 单线程调度
    ↓
协程池 (CoroutinePool) - 多线程执行
    ↓
线程池 (ThreadPool) - 底层工作线程
```

### 2. 主要组件

#### 2.1 CoroutineManager（协程管理器）

- **职责**: 协程生命周期管理、定时器处理、调度决策
- **线程模型**: 单线程，通常在主线程中运行
- **关键方法**:
  - `drive()`: 驱动协程调度循环
  - `schedule_resume()`: 调度协程恢复执行
  - `add_timer()`: 添加定时器任务

#### 2.2 CoroutinePool（协程池）

- **职责**: 协程任务队列管理、多线程调度
- **线程模型**: 多线程执行，使用线程池
- **关键方法**:
  - `schedule_coroutine()`: 将协程提交到线程池执行
  - `schedule_task()`: 调度CPU密集型任务

#### 2.3 SafeCoroutineHandle（安全协程句柄）

- **职责**: 多线程环境下的协程句柄安全管理
- **特性**: 防止跨线程访问冲突、资源竞争保护

## 调度流程详解

### 1. 协程创建和启动

```cpp
// 1. 创建协程任务
auto task = handle_single_request(user_id);

// 2. 协程在创建时挂起 (initial_suspend() 返回 std::suspend_always)
// 3. 调用 get() 方法启动协程
task.get(); // 或 co_await task
```

### 2. 调度决策链

```text
Task::get() 
  → 检查协程状态
  → CoroutineManager::schedule_resume()
  → schedule_coroutine_enhanced()
  → CoroutinePool::schedule_coroutine()
  → ThreadPool::enqueue_void()
  → 在工作线程中执行 handle.resume()
```

### 3. 多线程安全机制

#### 3.1 执行状态保护

```cpp
class ThreadSafeCoroutineState {
    std::atomic<bool> running_{false};      // 防止并发执行
    std::atomic<bool> destroyed_{false};    // 生命周期标记
    std::atomic<bool> scheduled_{false};    // 防止重复调度
    std::shared_ptr<std::mutex> execution_mutex_; // 执行锁
};
```

#### 3.2 RAII 执行保护

```cpp
struct ExecutionGuard {
    // 构造时获取执行权
    // 析构时释放执行权和重置调度状态
};
```

## 并发特性分析

### 1. 是否并发？

**是的，FlowCoro 支持真正的并发执行：**

- **协程级并发**: 多个协程可以在不同线程中同时执行
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
