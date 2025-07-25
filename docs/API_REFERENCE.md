# FlowCoro v4.0.0 API 参考手册

## 🎯 v4.0.0 精简核心版

### 架构精简与核心专注

- **精简设计**: 专注协程库核心功能，移除复杂组件
- **简洁API**: 提供直观易用的协程接口
- **高性能**: 优化的内存管理和无锁数据结构
- **跨平台**: 支持Linux、macOS、Windows

### 核心性能指标

基于精简架构的性能数据：
- **协程创建**: 轻量级协程任务创建
- **内存管理**: 高效的内存池分配
- **并发调度**: 协程池调度系统
- **无锁操作**: 无锁队列和数据结构

### 基础使用示例

```cpp
#include "flowcoro.hpp"

// 创建协程任务
flowcoro::Task<int> calculate(int x) {
    co_return x * x;
}

// 同步等待协程完成
int main() {
    auto result = flowcoro::sync_wait(calculate(5));
    std::cout << "结果: " << result << std::endl; // 输出: 25
    return 0;
}
```

## 📋 目录

### 核心模块
- [1. 协程核心 (core.h)](#1-协程核心-coreh) - Task接口、when_all并发、协程管理、同步等待
- [2. 线程池 (thread_pool.h)](#2-线程池-thread_poolh) - 高性能线程池实现
- [3. 内存管理 (memory.h)](#3-内存管理-memoryh) - 内存池、对象池
- [4. 无锁数据结构 (lockfree.h)](#4-无锁数据结构-lockfreeh) - 无锁队列、栈

### 系统模块  
- [5. 协程池](#5-协程池) - 协程调度和管理
- [6. 全局配置](#6-全局配置) - 系统配置和初始化

## 🚀 概述

FlowCoro v4.0.0 是一个精简的现代C++20协程库，专注于提供高效的协程核心功能。本版本移除了网络、数据库、RPC等复杂组件，专注于协程任务管理、内存管理、线程池和无锁数据结构等核心特性。

---

## 1. 协程核心 (core.h)

FlowCoro v4.0.0 提供精简的协程任务接口，专注于核心功能。

### Task<T> - 协程任务接口

FlowCoro的协程任务模板类，提供基础的协程功能。

```cpp
template<typename T = void>
class Task {
public:
    // 基础操作
    bool done() const noexcept;           // 协程是否完成
    T get_result();                       // 获取结果（阻塞）
    
    // 协程接口
    auto operator co_await();             // 使协程可等待
    
    // 移动语义
    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;
    
    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
private:
    std::coroutine_handle<> handle_;
};
```

#### 基础使用示例

```cpp
// 创建协程任务
Task<int> compute(int x) {
    co_return x * x;
}

// 同步等待结果
auto result = sync_wait(compute(5)); // result = 25

// 协程组合
Task<int> chain_compute() {
    auto result1 = co_await compute(3);
    auto result2 = co_await compute(result1);
    co_return result2; // 返回 81
}
```

### sync_wait - 同步等待协程

将异步协程转换为同步调用，阻塞等待协程完成。

```cpp
template<typename T>
T sync_wait(Task<T> task);

// 使用示例
Task<int> async_compute(int x) {
    co_return x * 2;
}

// 在main函数中同步等待
int main() {
    auto result = sync_wait(async_compute(21)); // result = 42
    std::cout << result << std::endl;
    return 0;
}
```

### when_all - 并发等待多个协程

并发执行多个协程任务并等待全部完成，支持不同类型的任务组合。

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
    auto [int_result, str_result, bool_result] = 
        co_await when_all(
            compute_int(5),           // 返回 25
            compute_string("hello"),   // 返回 "hello_processed"
            compute_bool()            // 返回 true
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
    // 并发执行多个相同类型的任务
    auto [r1, r2, r3, r4] = co_await when_all(
        heavy_compute(10),  // 返回 110
        heavy_compute(20),  // 返回 420
        heavy_compute(30),  // 返回 930
        heavy_compute(40)   // 返回 1640
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
- **大量任务**: 超过 100+ 个任务（推荐使用协程池）
- **动态任务**: 运行时确定任务数量
- **流式处理**: 需要处理任务完成顺序的场景
- **高并发场景**: 5000+ 任务必须使用协程池管理

#### 性能特点

- **零开销抽象**: 编译期优化，无运行时开销
- **类型安全**: 强类型返回值，编译期检查
- **内存高效**: 栈上分配，无动态内存分配
- **适度并发**: 最优性能区间 2-10 个任务

#### ⚠️ 重要限制：when_all 没有协程池

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
// ❌ 错误：when_all 无法处理大量任务
// auto result = co_await when_all(task1, task2, ..., task5000); // 编译失败！

// ✅ 正确：使用协程池处理大量任务
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
            may_fail(10),   // 成功: 20
            may_fail(-5)    // 抛出异常
        );
        // 不会执行到这里
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }
}
```

### 协程管理函数

```cpp
// 调度协程到协程池
void schedule_coroutine(std::coroutine_handle<> handle);

// 获取协程管理器实例
CoroutineManager& get_coroutine_manager();

// 驱动协程执行
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

协程调度和管理系统。

### CoroutineManager - 协程管理器

```cpp
class CoroutineManager {
public:
    static CoroutineManager& get_instance();
    
    // 调度协程
    void schedule(std::coroutine_handle<> handle);
    
    // 驱动协程执行
    void drive();
    
    // 获取统计信息
    size_t active_coroutines() const;
    size_t pending_coroutines() const;
    
private:
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex queue_mutex_;
};
```

### 协程调度函数

```cpp
// 调度协程到协程池
void schedule_coroutine_enhanced(std::coroutine_handle<> handle);

// 获取协程管理器
CoroutineManager& get_coroutine_manager();
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

// 初始化协程系统
void init_coroutine_system();

// 清理协程系统
void cleanup_coroutine_system();
```

---

## 🎯 完整使用示例

### 基础协程应用

```cpp
#include "flowcoro.hpp"

// 异步计算任务
flowcoro::Task<int> async_calculate(int x, int y) {
    // 模拟一些异步工作
    co_return x + y;
}

// 协程组合
flowcoro::Task<int> complex_calculation() {
    auto result1 = co_await async_calculate(10, 20);
    auto result2 = co_await async_calculate(result1, 30);
    co_return result2; // 返回 60
}

int main() {
    // 初始化协程系统
    flowcoro::init_coroutine_system();
    
    // 执行协程
    auto result = flowcoro::sync_wait(complex_calculation());
    std::cout << "计算结果: " << result << std::endl;
    
    // 清理资源
    flowcoro::cleanup_coroutine_system();
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

**注意**: 这是FlowCoro v4.0.0的API文档，专注于核心协程功能。如需了解完整的项目信息，请参考主README文档。
