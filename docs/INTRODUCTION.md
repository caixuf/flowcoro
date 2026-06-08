# FlowCoro 介绍

## 什么是 FlowCoro？

FlowCoro 是一个基于 **C++20 协程**的高性能异步编程库，专为批量任务处理和高吞吐量场景而设计。它将现代 C++ 协程技术与无锁数据结构、智能负载均衡相结合，提供了一套简洁而高效的并发编程接口。

简单来说，FlowCoro 让你可以用**看起来像同步代码的方式**编写高并发程序，同时获得接近甚至超越 Go、Rust 的运行时性能。

---

## 为什么需要 FlowCoro？

### 传统并发方案的困境

在高并发场景下，传统方案各有局限：

| 方案 | 优点 | 局限 |
|------|------|------|
| 多线程 | 真正并行 | 线程切换开销大，内存占用高，同步复杂 |
| 回调函数 | 非阻塞 | "回调地狱"，代码可读性差，调试困难 |
| Future/Promise | 链式调用 | 仍然复杂，性能不佳 |
| 操作系统异步I/O | 性能好 | 编程模型复杂，平台差异大 |

### C++20 协程：优雅的解决方案

C++20 引入了原生协程支持（`co_await`、`co_return`、`co_yield`），让我们可以：

```cpp
// 写起来像同步代码...
Task<int> fetch_data(int id) {
    co_await sleep_for(std::chrono::milliseconds(10));  // 不阻塞线程
    co_return id * 2;
}

// ...但实际上是高效的异步执行
Task<void> process() {
    auto r1 = co_await fetch_data(1);  // 在等待时让出线程
    auto r2 = co_await fetch_data(2);
    std::cout << r1 + r2 << std::endl;
}
```

### FlowCoro 的价值

FlowCoro 在 C++20 协程的基础上，提供了：

- **完整的调度系统**：多层无锁调度器，让协程高效运行
- **智能负载均衡**：自动将协程分配到最优调度器
- **Channel 通信**：类 Go 语言的协程间通信机制
- **内存池优化**：减少频繁的内存分配开销
- **PGO 优化支持**：通过编译器引导的优化进一步提升性能

---

## 核心概念

### 1. Task\<T\> — 协程任务

`Task<T>` 是 FlowCoro 的核心类型，表示一个可以被 `co_await` 的异步任务：

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 返回 int 的异步任务
Task<int> add(int a, int b) {
    co_return a + b;  // 使用 co_return 返回结果
}

// 不返回值的异步任务
Task<void> print_hello() {
    std::cout << "Hello, FlowCoro!\n";
    co_return;
}
```

**关键特性**：`Task` 在创建时会立即在调用者线程上同步执行，直到遇到第一个 `co_await` 挂起点，之后才进入调度系统异步执行。这种 **suspend_never** 机制减少了不必要的调度开销。

### 2. sync_wait() — 从普通函数进入协程世界

在 `main()` 等普通函数中，使用 `sync_wait()` 来阻塞等待一个协程任务完成：

```cpp
int main() {
    // 阻塞等待协程完成，并获取返回值
    int result = sync_wait(add(3, 4));
    std::cout << "3 + 4 = " << result << std::endl;  // 输出: 3 + 4 = 7
    return 0;
}
```

### 3. co_await — 异步等待

在协程内部，使用 `co_await` 等待另一个任务完成，同时不阻塞底层线程：

```cpp
Task<void> concurrent_example() {
    // 同时启动三个任务（它们在挂起点后并发执行）
    auto task1 = add(1, 2);
    auto task2 = add(3, 4);
    auto task3 = add(5, 6);

    // 依次等待结果
    int r1 = co_await task1;  // 等待 task1 完成
    int r2 = co_await task2;  // 等待 task2 完成
    int r3 = co_await task3;  // 等待 task3 完成

    std::cout << r1 << ", " << r2 << ", " << r3 << std::endl;  // 3, 7, 11
}
```

### 4. sleep_for() — 协程友好的延时

与 `std::this_thread::sleep_for()` 不同，`flowcoro::sleep_for()` 挂起当前协程而不阻塞底层线程，使线程可以在等待期间处理其他任务：

```cpp
Task<void> delayed_print(int ms, const std::string& msg) {
    co_await sleep_for(std::chrono::milliseconds(ms));
    std::cout << msg << std::endl;
}
```

### 5. Channel — 协程间通信

Channel 提供了一种线程安全的方式在协程之间传递数据，类似于 Go 语言的 channel：

```cpp
Task<void> producer_consumer() {
    auto ch = make_channel<int>(10);  // 容量为 10 的缓冲通道

    // 生产者协程
    auto producer = [ch]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await ch->send(i);  // 发送数据，通道满时挂起等待
            std::cout << "发送: " << i << "\n";
        }
        ch->close();  // 关闭通道，通知消费者结束
    };

    // 消费者协程
    auto consumer = [ch]() -> Task<void> {
        while (true) {
            auto val = co_await ch->recv();  // 接收数据，通道空时挂起等待
            if (!val.has_value()) break;     // 通道关闭时退出
            std::cout << "接收: " << val.value() << "\n";
        }
    };

    co_await producer();
    co_await consumer();
}
```

### 6. when_any() 和 when_all() — 并发控制

- **`when_any()`**：等待多个任务中任意一个完成，返回第一个完成的结果和其索引
- **`when_all()`**：等待所有任务完成，返回所有结果的集合

```cpp
Task<void> race_example() {
    auto slow_task = []() -> Task<int> {
        co_await sleep_for(std::chrono::milliseconds(100));
        co_return 1;
    };
    auto fast_task = []() -> Task<int> {
        co_await sleep_for(std::chrono::milliseconds(10));
        co_return 2;
    };

    // 等待最快完成的任务
    auto [result, index] = co_await when_any(slow_task(), fast_task());
    std::cout << "最先完成的任务索引: " << index
              << "，结果: " << result << "\n";  // 索引: 1，结果: 2
}
```

### 7. yield() — 协作式调度

`yield()` 让当前协程主动让出执行权，允许调度器处理其他等待中的任务，适用于长时间运行的计算任务：

```cpp
Task<int> heavy_computation(int n) {
    int result = 0;
    for (int i = 0; i < n; ++i) {
        result += i;
        if (i % 1000 == 0) {
            co_await yield();  // 每 1000 次迭代主动让出，保证调度公平性
        }
    }
    co_return result;
}
```

---

## 三层调度架构

FlowCoro 采用三层调度架构，确保高吞吐量和低延迟：

```
应用层协程 (Task<T>)
      │
      │ suspend_never: 同步执行至首个挂起点
      ▼
协程管理器 (CoroutineManager)
      │
      │ 智能负载均衡：选择最优调度器
      ▼
协程池 (CoroutinePool) ── 多个独立的 CoroutineScheduler
      │
      │ 无锁队列分发
      ▼
线程池 (ThreadPool) ── 工作线程并行执行协程
```

### 工作流程

1. **创建 Task**：协程在调用者线程上立即同步执行
2. **遇到 co_await**：协程挂起，控制权返回给调用者
3. **进入调度系统**：协程句柄被投递到负载最轻的调度器队列
4. **工作线程恢复**：线程池中的工作线程从无锁队列取出协程并恢复执行
5. **完成通知**：协程完成后唤醒等待它的上层协程

### 关键设计决策

- **无锁队列**：所有调度队列均使用 lock-free 数据结构，避免锁竞争
- **CPU 亲和性**：每个调度器绑定特定 CPU 核心，减少缓存失效
- **批量处理**：调度循环每次批量处理多个协程，降低每个协程的调度开销
- **内存池**：协程帧使用专用内存池分配，减少 `malloc`/`free` 调用

---

## 完整示例：HTTP 请求批处理器

以下是一个综合示例，展示 FlowCoro 在批量处理场景中的优势：

```cpp
#include <flowcoro.hpp>
#include <iostream>
#include <vector>
using namespace flowcoro;

// 模拟单个 HTTP 请求处理
Task<std::string> handle_request(int request_id) {
    // 模拟 I/O 等待（如数据库查询、网络请求）
    co_await sleep_for(std::chrono::milliseconds(50));

    // 返回处理结果
    co_return "Response-" + std::to_string(request_id);
}

// 批量处理请求
Task<void> batch_processor(int total_requests) {
    std::vector<Task<std::string>> tasks;

    // 同时启动所有请求任务
    tasks.reserve(total_requests);
    for (int i = 0; i < total_requests; ++i) {
        tasks.push_back(handle_request(i));
    }

    // 收集所有结果
    int success_count = 0;
    for (auto& task : tasks) {
        auto response = co_await task;
        ++success_count;
    }

    std::cout << "处理完成: " << success_count << "/" << total_requests
              << " 个请求\n";
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    sync_wait(batch_processor(1000));  // 并发处理 1000 个请求

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "总耗时: " << ms << " ms\n";
    // 协程: ~50ms (并发)，多线程: ~数百ms (受线程数限制)

    return 0;
}
```

---

## 性能表现

FlowCoro 在关键指标上与 Go 和 Rust (Tokio) 相比：

| 指标 | FlowCoro | Go | Rust (Tokio) |
|------|----------|----|--------------|
| 协程/任务创建 | **13.3M ops/s** | 1.86M ops/s | 36.4K ops/s |
| 并发客户端 (100任务) | **183K ops/s** | 2.9K ops/s | 854 ops/s |
| 无锁队列操作 | **15.8M ops/s** | 11.0M ops/s | 7.2M ops/s |
| 内存池分配 | **35.2M ops/s** | 32.7M ops/s | 35.0M ops/s |
| HTTP 请求处理 | **34.9M ops/s** | 33.0M ops/s | 34.6M ops/s |

> 详细测试方法和环境见 [性能数据参考](PERFORMANCE_DATA.md)。

---

## 适用场景

### 推荐使用 FlowCoro 的场景

- **高并发 Web 服务**：大量短连接请求的 HTTP/API 服务器
- **批量数据处理**：同时处理数千个独立的 I/O 密集型任务
- **微服务网关**：转发大量并发请求到后端服务
- **高频交易系统**：低延迟、高吞吐的金融数据处理
- **生产者-消费者管道**：通过 Channel 构建数据流处理链
- **爬虫/批量抓取**：并发抓取大量 URL

### 不适合的场景

- **需要精确执行顺序**：suspend_never 机制意味着任务在创建时立即执行，不适合需要精确控制执行时序的场景
- **单一长时间 CPU 密集型任务**：对于单个长时间运算任务，多线程更合适
- **手动管理协程生命周期**：FlowCoro 自动管理生命周期，若需要精细手动控制则不适用

---

## 快速上手

### 1. 编译安装

```bash
git clone https://github.com/caixuf/flowcoro.git
cd flowcoro
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 2. 在项目中使用

在 `CMakeLists.txt` 中添加：

```cmake
find_package(flowcoro REQUIRED)
target_link_libraries(your_target flowcoro::flowcoro)
```

或者直接包含头文件目录（header-only 模式）：

```cmake
target_include_directories(your_target PRIVATE /path/to/flowcoro/include)
target_compile_features(your_target PRIVATE cxx_std_20)
```

### 3. Hello World

```cpp
#include <flowcoro.hpp>
#include <iostream>
using namespace flowcoro;

Task<void> hello() {
    co_await sleep_for(std::chrono::milliseconds(1));
    std::cout << "Hello, FlowCoro!\n";
}

int main() {
    sync_wait(hello());
    return 0;
}
```

---

## 进一步学习

掌握了基础之后，可以通过以下文档深入了解 FlowCoro：

| 文档 | 内容 |
|------|------|
| [快速开始指南](QUICK_START.md) | 详细的安装、构建和基本使用 |
| [API 参考](API_REFERENCE.md) | 完整的 API 文档和使用示例 |
| [架构设计](ARCHITECTURE.md) | 三层调度架构的详细说明 |
| [性能数据](PERFORMANCE_DATA.md) | 基准测试结果和对比分析 |
| [PGO 优化指南](PGO_GUIDE.md) | 通过 Profile-Guided Optimization 进一步提升性能 |

---

## 总结

FlowCoro 的核心价值在于：

1. **开发效率**：用接近同步的代码风格编写高并发程序，降低认知负担
2. **运行性能**：无锁架构 + 智能负载均衡 + 内存池，实现卓越的吞吐量
3. **现代 C++**：充分利用 C++20 协程特性，代码简洁可维护
4. **生产就绪**：经过性能基准测试验证，适用于真实的生产环境

如果你正在构建需要处理大量并发 I/O 任务的 C++ 应用，FlowCoro 是一个值得尝试的选择。
