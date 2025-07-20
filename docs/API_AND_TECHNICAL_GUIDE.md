# FlowCoro API 与技术指南

## 概述

FlowCoro 提供了一套完整的现代 C++20 协程编程接口，包括协程核心、无锁数据结构、线程池、网络 IO、日志系统和内存管理等模块。本指南将介绍核心 API 的使用及其背后的技术实现。

## 核心模块

### 1. 协程核心 (core.h)

#### Task<T>
协程任务模板类，支持 co_await 语法。

```cpp
template<typename T>
class Task {
public:
    // 构造函数
    Task(std::coroutine_handle<Promise> handle);
    
    // 获取结果 (阻塞等待)
    T get();
    
    // 异步等待 (协程中使用)
    auto operator co_await();
    
    // 检查是否完成
    bool is_ready() const;
};
```

**特化版本：**
- `Task<void>` - 无返回值的协程
- `Task<std::unique_ptr<T>>` - 返回智能指针的协程

#### AsyncPromise<T>
协程 promise 类型，管理协程的生命周期和结果。

```cpp
template<typename T>
class AsyncPromise {
public:
    // 标准协程接口
    Task<T> get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_always final_suspend() noexcept;
    void unhandled_exception();
    
    // 返回值设置
    void return_value(const T& value);  // Task<T>
    void return_void();                 // Task<void>
```

## 技术架构

### 核心技术栈

| 技术领域 | 实现方案 | 性能指标 |
|---------|----------|----------|
| **协程引擎** | C++20 原生协程 | 创建 147ns，执行 9ns |
| **无锁编程** | CAS + 内存序 | 600 万+ ops/秒 |
| **网络 IO** | epoll + 协程化 | 10 万+ 并发连接 |
| **内存管理** | 对象池 + 内存池 | 403ns 分配速度 |
| **日志系统** | 异步 + 无锁队列 | 250 万条/秒 |

### 架构分层

```
┌─────────────────────────────────┐
│        用户应用层                │  ← 业务逻辑
├─────────────────────────────────┤
│    网络 IO 层 (net.h)           │  ← 异步 Socket/TCP/HTTP
├─────────────────────────────────┤
│   协程调度层 (core.h)           │  ← Task/Promise/调度器
├─────────────────────────────────┤
│  执行引擎 (thread_pool.h)       │  ← 工作窃取线程池
├─────────────────────────────────┤
│ 无锁数据结构 (lockfree.h)       │  ← Queue/Stack/RingBuffer
├─────────────────────────────────┤
│ 基础设施 (logger.h, memory.h)  │  ← 日志/内存/缓冲区
└─────────────────────────────────┘
```

### 1. 协程系统 (core.h)

**设计亮点：**
- 完全基于 C++20 标准协程，零运行时开销
- Task<T> 模板支持任意返回类型，包括 void 和 unique_ptr 特化
- AsyncPromise 提供完整的协程生命周期管理
- 支持协程异常传播和资源自动清理

**技术实现：**
```cpp
template<typename T>
class Task {
    std::coroutine_handle<AsyncPromise<T>> handle_;
```
