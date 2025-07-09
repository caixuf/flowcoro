# FlowCoro

FlowCoro 是一个基于 C++20 协程的现代异步编程库，旨在简化异步操作和协程管理，适用于需要高性能异步处理的应用场景。

## 项目概述

FlowCoro 提供了一组轻量级的协程工具和网络请求抽象接口，通过 C++20 的原生协程支持，实现了简洁高效的异步编程模型。本项目适合用于开发高并发、低延迟的服务端应用或客户端异步任务调度。

### 核心组件

| 组件 | 描述 |
|------|------|
| `flowcoro.h` | 协程核心实现，定义了协程基础类型与执行逻辑 |
| `http_request.h` | 网络请求抽象接口及 libcurl 实现 |
| `thread_pool.h` | 线程池实现，用于管理协程调度 |
| `memory_pool.h` / `object_pool.h` | 高性能内存与对象池，优化资源分配 |
| `test/coro_tests.cpp` | 单元测试套件 |

## 协程原理

FlowCoro 利用 **C++20 原生协程**（Coroutines） 构建异步执行流程。其底层基于标准库 `<coroutine>` 实现，并通过封装使其更易于使用。

### 协程生命周期

1. **创建**：通过 lambda 表达式创建协程并返回 `CoroTask`
2. **挂起**：协程在启动后可自行挂起，等待事件触发继续执行
3. **恢复**：调用 `resume()` 恢复被挂起的协程
4. **销毁**：协程执行完成后自动销毁或手动销毁

### 关键结构

- `CoroTask`：表示一个无返回值的协程任务
- `Task<T>`：支持返回值的协程任务
- `INetworkRequest`：网络请求抽象接口，支持扩展多种实现（如 Mock、libcurl 等）

## 主要封装设计

### 协程封装

FlowCoro 对协程进行了如下封装，使其更易于使用和集成到实际项目中：

- **生命周期管理**：通过 RAII 模式管理协程句柄的创建与销毁
- **异步执行**：提供 `co_await` 支持的异步操作接口
- **错误处理**：统一的异常捕获机制，防止协程内异常导致程序崩溃

### 网络请求封装

网络请求模块采用面向接口的设计，便于替换底层实现。默认提供了基于 libcurl 的实现，并支持自定义实现，例如：

```cpp
// 使用默认 libcurl 请求
std::string response = co_await flowcoro::CoroTask::execute_network_request<flowcoro::HttpRequest>("http://example.com");
```

同时，你可以轻松实现自定义网络请求类，只需继承 `INetworkRequest` 接口即可。

## 构建要求

- C++20 编译器 (GCC 10+, Clang 12+, MSVC 2022+)
- CMake 3.14+
- 支持协程的标准库

## 构建方法

```bash
# 创建构建目录
mkdir build

# 进入构建目录
cd build

# 运行CMake配置
cmake ..

# 构建项目
make
```

## 运行测试

```bash
# 运行单元测试
./run_tests.sh
```

## 许可证

本项目采用 MIT 许可证。详情请查看 LICENSE 文件。

## 无锁编程升级 (v2.0)

FlowCoro v2.0 全面升级为使用 **C++20 无锁编程技术**，提供了更高的性能和并发能力。

### 无锁数据结构

| 数据结构 | 描述 | 特性 |
|----------|------|------|
| `lockfree::Queue<T>` | 无锁队列 (Michael & Scott算法) | 多生产者多消费者安全 |
| `lockfree::Stack<T>` | 无锁栈 (Treiber Stack) | 高性能LIFO操作 |
| `lockfree::RingBuffer<T, Size>` | 无锁环形缓冲区 | 单生产者单消费者，零拷贝 |
| `lockfree::AtomicCounter` | 原子计数器 | 高性能计数操作 |

### 无锁线程池

- `lockfree::ThreadPool`: 基础无锁线程池
- `lockfree::WorkStealingThreadPool`: 工作窃取线程池，提供更好的负载均衡

### 协程调度器

- `GlobalThreadPool`: 全局无锁线程池管理器
- 协程自动调度到无锁线程池执行
- 支持异步Promise和协程间通信

### 性能特性

- ✅ **零锁设计**: 所有数据结构使用原子操作，避免锁竞争
- ✅ **内存对齐**: 关键数据结构使用64字节对齐，优化缓存性能
- ✅ **内存序**: 精确的内存序控制，确保正确性和性能
- ✅ **工作窃取**: 智能任务分发，提高CPU利用率

### 无锁编程示例

```cpp
#include "flowcoro.h"
#include "lockfree.h"

// 无锁队列示例
lockfree::Queue<int> queue;
queue.enqueue(42);
int value;
if (queue.dequeue(value)) {
    std::cout << "Dequeued: " << value << std::endl;
}

// 无锁协程示例
auto coro_task = []() -> flowcoro::CoroTask {
    // 异步操作
    auto result = co_await flowcoro::sleep_for(std::chrono::milliseconds(100));
    
    // 使用无锁Promise
    flowcoro::AsyncPromise<std::string> promise;
    promise.set_value("Hello, lockfree world!");
    std::string msg = co_await promise;
    
    co_return;
}();

coro_task.resume();
```