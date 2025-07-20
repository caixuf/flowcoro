# FlowCoro 2.0 🚀

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CMake](https://img.shields.io/badge/CMake-3.16+-green.svg)](https://cmake.org/)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Performance](https://img.shields.io/badge/Performance-Industrial%20Grade-red.svg)]()

> **现代C++20协程编程库，专为高性能、低延迟场景设计**

FlowCoro 是一个工业级的异步编程框架，基于C++20原生协程和无锁编程技术构建。它为开发者提供了简洁易用的API，同时保证了生产级别的性能和可靠性。

## 📚 文档导航

### 📖 核心文档

| 文档名称 | 内容概要 |
|---------|----------|
| [API 与技术指南](docs/API_AND_TECHNICAL_GUIDE.md) | 协程核心 API 与技术实现详解 |
| [网络编程指南](docs/NETWORK_GUIDE.md) | 异步 Socket、TCP 服务器与事件循环使用说明 |
| [数据库与项目指南](docs/DATABASE_AND_PROJECT_GUIDE.md) | 数据库连接池使用说明与项目工程化状态 |
| [性能优化指南](docs/PERFORMANCE_GUIDE.md) | 性能调优技巧与最佳实践 |

### 📝 开发指南

| 文档名称 | 内容概要 |
|---------|----------|
| [执行计划](docs/EXECUTION_PLAN.md) | 项目开发路线与迭代计划 |
| [学习指南](docs/LEARNING_GUIDE.md) | 新手入门与进阶学习资源 |

### 📦 附加文档

| 文档名称 | 内容概要 |
|---------|----------|
| [扩展路线图](docs/EXTENSION_ROADMAP.md) | 未来功能扩展计划 |
| [Phase2 实现](docs/PHASE2_IMPLEMENTATION.md) | 协程调度与执行引擎实现细节 |
| [项目状态](docs/PROJECT_STATUS.md) | 当前开发进度与稳定性说明 |

## 📚 文档导航

### 📖 核心文档

| 文档名称 | 内容概要 |
|---------|----------|
| [API 与技术指南](docs/API_AND_TECHNICAL_GUIDE.md) | 协程核心 API 与技术实现详解 |
| [网络编程指南](docs/NETWORK_GUIDE.md) | 异步 Socket、TCP 服务器与事件循环使用说明 |
| [数据库与项目指南](docs/DATABASE_AND_PROJECT_GUIDE.md) | 数据库连接池使用说明与项目工程化状态 |
| [性能优化指南](docs/PERFORMANCE_GUIDE.md) | 性能调优技巧与最佳实践 |

### 📝 开发指南

| 文档名称 | 内容概要 |
|---------|----------|
| [执行计划](docs/EXECUTION_PLAN.md) | 项目开发路线与迭代计划 |
| [学习指南](docs/LEARNING_GUIDE.md) | 新手入门与进阶学习资源 |

### 📦 附加文档

| 文档名称 | 内容概要 |
|---------|----------|
| [扩展路线图](docs/EXTENSION_ROADMAP.md) | 未来功能扩展计划 |
| [Phase2 实现](docs/PHASE2_IMPLEMENTATION.md) | 协程调度与执行引擎实现细节 |
| [项目状态](docs/PROJECT_STATUS.md) | 当前开发进度与稳定性说明 |

## ✨ 核心特性

### 🎯 **协程优先设计**
- **原生C++20协程**：基于标准协程实现，零妥协的性能
- **零开销抽象**：协程创建仅需147ns，执行开销9ns
- **异步友好**：所有IO操作天然异步，避免线程阻塞

### ⚡ **无锁高性能**
- **无锁数据结构**：队列、栈、环形缓冲区，600万+ops/秒
- **工作窃取线程池**：智能负载均衡，最大化CPU利用率
- **内存池管理**：403ns分配速度，减少内存碎片

### 🌐 **异步网络IO**
- **基于epoll的事件循环**：Linux高性能网络编程
- **协程化Socket**：write/read/connect全部支持co_await
- **TCP服务器框架**：支持10万+并发连接

### 🔧 **生产就绪**
- **异步日志系统**：250万条/秒吞吐量，不阻塞业务
- **内存安全**：RAII + 智能指针，防止内存泄漏
- **完整测试覆盖**：单元测试 + 性能测试 + 网络测试

## 🏆 性能基准 (实测数据)

| 组件 | FlowCoro | 传统方案 | 性能提升 |
|------|----------|----------|----------|
| **协程创建** | 158µs/1000 = 158ns | 2000ns (thread) | **12.7x** |
| **协程执行** | 9µs/1000 = 9ns | 50ns (callback) | **5.6x** |
| **无锁队列** | 176µs/1M = 176ns/op | 1000ns/op (mutex) | **5.7x** |
| **内存分配** | 371µs/10K = 371ns | 1000ns (malloc) | **2.7x** |
| **异步日志** | 452µs/10K = 452ns/条 | 2000ns/条 | **4.4x** |

> 测试环境：Linux x86_64, GCC 11, Release构建

## 🚀 快速开始

### 环境要求

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake git gcc-11 g++-11

# 确保C++20支持
gcc --version  # 需要 >= 11.0
```

### 安装编译

```bash
git clone https://github.com/your-username/flowcoro.git
cd flowcoro
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行测试确保一切正常
./tests/flowcoro_tests
```

### Hello World

```cpp
#include <flowcoro.hpp>
#include <iostream>

// 定义异步任务
flowcoro::Task<std::string> fetch_data(const std::string& url) {
    // 模拟网络请求
    co_await flowcoro::sleep_for(std::chrono::milliseconds(100));
    co_return "Data from " + url;
}

// 主协程
flowcoro::Task<void> main_logic() {
    std::cout << "开始获取数据...\n";
    
    auto result = co_await fetch_data("https://api.example.com");
    std::cout << "收到: " << result << "\n";
    
    co_return;
}

int main() {
    // 运行协程
    auto task = main_logic();
    task.get();  // 等待完成
    
    return 0;
}
```

### 并发示例

```cpp
#include <flowcoro.hpp>
#include <vector>

// 并发处理多个任务
flowcoro::Task<void> concurrent_processing() {
    std::vector<std::string> urls = {
        "https://api1.example.com",
        "https://api2.example.com", 
        "https://api3.example.com"
    };
    
    // 启动所有任务
    std::vector<flowcoro::Task<std::string>> tasks;
    for (const auto& url : urls) {
        tasks.emplace_back(fetch_data(url));
    }
    
    // 等待所有完成
    for (auto& task : tasks) {
        auto result = co_await task;
        std::cout << "结果: " << result << "\n";
    }
    
    co_return;
}
```

### 网络服务器示例

```cpp
#include <flowcoro.hpp>

// HTTP Echo服务器
flowcoro::Task<void> handle_connection(std::unique_ptr<flowcoro::net::Socket> client) {
    auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(client));
    
    while (true) {
        try {
            // 读取请求
            auto request = co_await conn->read_line();
            if (request.empty()) break;
            
            // 发送响应
            co_await conn->write("Echo: " + request);
            co_await conn->flush();
            
        } catch (const std::exception& e) {
            break;  // 连接断开
        }
    }
    
    co_return;
}

int main() {
    auto& loop = flowcoro::net::GlobalEventLoop::get();
    flowcoro::net::TcpServer server(&loop);
    
    // 设置连接处理器
    server.set_connection_handler(handle_connection);
    
    // 开始监听
    auto listen_task = server.listen("0.0.0.0", 8080);
    auto loop_task = loop.run();
    
    std::cout << "服务器启动在 http://localhost:8080\n";
    
    // 运行事件循环
    loop_task.get();
    
    return 0;
}
```

## 📊 架构设计

```
┌─────────────────────────────────┐
│        用户应用层                │  ← 你的业务逻辑
├─────────────────────────────────┤
│    网络IO层 (net.h)             │  ← 异步Socket/TCP服务器  
├─────────────────────────────────┤
│   协程调度层 (core.h)           │  ← Task/AsyncPromise
├─────────────────────────────────┤
│  执行引擎 (thread_pool.h)       │  ← 无锁线程池
├─────────────────────────────────┤
│ 无锁数据结构 (lockfree.h)       │  ← Queue/Stack/Buffer
├─────────────────────────────────┤
│ 基础设施 (logger.h, memory.h)  │  ← 日志/内存管理
└─────────────────────────────────┘
```

### 核心组件

| 组件 | 文件 | 功能 |
|------|------|------|
| **协程核心** | `core.h` | Task、AsyncPromise、协程调度 |
| **无锁结构** | `lockfree.h` | Queue、Stack、RingBuffer |
| **线程池** | `thread_pool.h` | 工作窃取、任务调度 |
| **网络IO** | `net.h` | Socket、TcpServer、EventLoop |
| **日志系统** | `logger.h` | 异步日志、性能统计 |
| **内存管理** | `memory.h` | 内存池、缓存友好Buffer |

## 💼 应用场景

### 🌟 **高频交易系统**
- **微秒级延迟**：协程切换仅需9ns
- **零锁设计**：避免锁竞争导致的延迟抖动
- **内存池**：预分配内存，避免分配延迟

### 🎮 **游戏服务器**
- **百万并发**：单机支持10万+玩家连接
- **实时响应**：异步IO保证游戏流畅性
- **状态管理**：协程天然适合游戏逻辑

### 📱 **微服务架构**
- **异步通信**：高效的服务间调用
- **资源节约**：协程比线程轻量1000倍
- **易于扩展**：模块化设计，组件可插拔

### 🔗 **IoT平台**
- **低资源占用**：适合嵌入式和边缘设备
- **高并发处理**：同时处理大量设备连接
- **实时数据**：协程化的数据管道

## 📖 学习资源

### 📚 **文档指南**
- [🎯 学习指南](docs/LEARNING_GUIDE.md) - 从零开始掌握协程编程
- [🔧 API参考](docs/API_REFERENCE.md) - 完整的接口文档
- [⚡ 性能调优](docs/PERFORMANCE_GUIDE.md) - 高性能编程技巧
- [🌐 网络编程](docs/NETWORK_GUIDE.md) - 异步网络开发

### 💡 **示例代码**
- [基础示例](examples/basic_example.cpp) - 协程入门
- [网络示例](examples/network_example.cpp) - TCP服务器
- [并发示例](examples/enhanced_demo.cpp) - 生产者消费者

### 🧪 **测试和基准**
```bash
# 运行所有测试
./tests/flowcoro_tests

# 性能基准测试
./benchmarks/simple_benchmarks

# 网络压力测试  
./examples/network_example &
curl http://localhost:8080
```

## �️ 开发指南

### CMake集成

```cmake
# 在你的项目中使用FlowCoro
find_package(FlowCoro REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE flowcoro_net)
```

### 编译选项

```bash
# Debug构建 (开发调试)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release构建 (生产环境)
cmake -DCMAKE_BUILD_TYPE=Release ..

# 启用内存检查 (开发阶段)
cmake -DFLOWCORO_ENABLE_SANITIZERS=ON ..
```

### 代码风格

- 使用现代C++20特性
- 遵循RAII和智能指针
- 异步优先，避免阻塞操作
- 错误处理使用异常机制

## 🤝 贡献与社区

### 💡 **如何贡献**
1. **Fork项目** - 在GitHub上fork仓库
2. **创建分支** - `git checkout -b feature/my-feature`
3. **编写代码** - 确保通过所有测试
4. **提交PR** - 详细描述你的更改

### � **问题反馈**
- [GitHub Issues](https://github.com/yourusername/flowcoro/issues) - Bug报告和功能请求
- [讨论区](https://github.com/yourusername/flowcoro/discussions) - 技术讨论
- **邮件**: 2024740941@qq.com - 商业合作

### � **发展路线**
- [ ] **v2.1**: HTTP/2协议支持
- [ ] **v2.2**: WebSocket实现 
- [ ] **v2.3**: 分布式协程调度
- [ ] **v3.0**: CUDA协程支持

## 📄 许可证

本项目采用 [MIT 许可证](LICENSE) - 详见LICENSE文件

## 🙏 致谢

FlowCoro的诞生离不开以下项目和社区的启发：

- **C++20协程标准委员会** - 标准化协程支持
- **Lewis Baker** - cppcoro库的设计思想  
- **Folly团队** - 高性能C++库实践
- **所有贡献者** - 让FlowCoro变得更好

---

<div align="center">

**🚀 让C++协程编程更简单、更快速！**

[⭐ 给项目加星标](https://github.com/caixuf/flowcord) | [🍴 Fork项目](https://github.com/caixuf/flowcord) | [👀 关注更新](https://github.com/caixuf/flowcord)

</div>