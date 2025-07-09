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

- C++20 兼容编译器（如 GCC 10+，Clang 12+）
- CMake 3.14 或更高版本
- 线程支持
- libcurl 开发库（若使用网络功能）

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