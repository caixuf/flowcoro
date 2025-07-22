# FlowCoro# FlowCoro v2.3

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CMake](https://img.shields.io/badge/CMake-3.16+-green.svg)](https://cmake.org/)

> **个人C++20协程学习项目，探索现代异步编程**

这是我学习C++20协程特性的个人项目，实现了完整的异步编程组件。项目重点在于理解协程工作原理、生命周期管理等核心概念，通过实践加深对现代异步编程的理解。

## 🏆 版本 v2.3 - SafeTask集成与项目整理

在这个版本中完成了：

### 🔧 主要功能实现

- ✅ **SafeTask实现**: 基于async_simple最佳实践的协程包装器
- ✅ **核心整合**: 所有协程功能统一整合到core.h
- ✅ **项目清理**: 删除重复文件，简化项目结构
- ✅ **RAII管理**: CoroutineScope自动生命周期管理
- ✅ **Promise风格API**: 类似JavaScript的状态查询接口
- ✅ **安全销毁机制**: 防止协程句柄损坏的safe_destroy()
- ✅ **跨线程支持**: 协程可以在任意线程恢复执行
- 📝 **文档更新**: 反映最新的项目状态.2

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CMake](https://img.shields.io/badge/CMake-3.16+-green.svg)](https://cmake.org/)

> **个人C++20协程学习项目，探索现代异步编程**

这是我学习C++20协程特性的个人项目，实现了基础的异步编程组件。项目重点在于理解协程工作原理、生命周期管理等核心概念，通过实践加深对现代异步编程的理解。

## � 版本 v2.2 - Task生命周期管理学习

在这个版本中主要学习和实现了：

### 🔧 主要功能实现

- ✅ **增强Task管理**: 原子状态跟踪 + 线程安全保护
- ✅ **Promise风格API**: 类似JavaScript的状态查询接口
- ✅ **安全销毁机制**: 防止协程句柄损坏的safe_destroy()
- ✅ **生命周期跟踪**: 创建时间戳和生命期统计
- ✅ **基础异步IO**: 网络和数据库的简单异步封装
- 📝 **文档完善**: 更新文档以反映真实学习进度

### 📊 当前组件状态

| 组件 | 状态 | 说明 |
|------|------|------|
| SafeTask系统 | ✅ 完成 | 基于async_simple最佳实践的协程包装器 |
| Task生命周期 | ✅ 完成 | 原子状态管理 + 线程安全销毁机制 |
| 协程核心系统 | ✅ 整合完成 | 所有功能统一在core.h中，简化结构 |
| 编译系统 | ✅ 可用 | CMake构建，无警告编译 |
| 网络组件 | ✅ 基础可用 | 异步Socket基础封装 |
| 数据库组件 | ✅ 基础可用 | 简单连接池实现 |
| RPC组件 | ✅ 基础可用 | JSON-RPC基础支持 |
| 测试覆盖 | ✅ 完整 | 核心功能单元测试，SafeTask演示 |

## 🚀 快速开始

### 环境要求

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake git gcc-11 g++-11

# 确保C++20支持
gcc --version  # 需要 >= 11.0
```

### 编译运行

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行SafeTask演示
./examples/improved_safe_task_demo
```

### 核心示例

#### SafeTask基础使用

```cpp
#include <flowcoro.hpp>

// SafeTask基础协程
flowcoro::SafeTask<int> compute_async() {
    co_await flowcoro::sleep_for(std::chrono::milliseconds(100));
    co_return 42;
}

// 链式协程操作
flowcoro::SafeTask<int> chain_computation(int input) {
    auto step1 = co_await compute_async();
    auto step2 = co_await compute_async(); 
    co_return step1 + step2 + input;
}

int main() {
    // 同步等待结果
    auto result = flowcoro::sync_wait(compute_async());
    std::cout << "结果: " << result << std::endl;
    
    return 0;
}
```
cd flowcord
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行测试
./tests/test_core
```

### 简单示例

```cpp
#include "flowcoro.hpp"

// 基础协程任务
flowcoro::Task<std::string> fetch_data() {
    co_await flowcoro::sleep_for(std::chrono::milliseconds(100));
    co_return "Hello FlowCoro!";
}

int main() {
    auto task = fetch_data();
    
    // Promise风格的状态查询 (v2.2新增)
    if (task.is_pending()) {
        std::cout << "Task running..." << std::endl;
    }
    
    auto result = task.get();
    std::cout << result << std::endl;
    
    return 0;
}
```

### 🆕 Promise风格状态查询API (v2.2学习实现)

学习JavaScript Promise设计，实现了状态查询接口：

```cpp
#include "flowcoro.hpp"

flowcoro::Task<int> async_computation(int value) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return value * 2;
}

int main() {
    auto task = async_computation(42);
    
    // JavaScript Promise风格的状态查询
    if (task.is_pending()) {
        std::cout << "Task is still running..." << std::endl;
    }
    
    // 等待完成
    auto result = task.get();
    
    // 检查最终状态
    if (task.is_fulfilled()) {
        std::cout << "Task completed successfully with: " << result << std::endl;
    } else if (task.is_rejected()) {
        std::cout << "Task failed or was cancelled." << std::endl;
    }
    
    return 0;
}
```

**学习实现的状态查询方法：**

| 方法 | 说明 | JavaScript对应 |
|------|------|---------------|
| `is_pending()` | 任务正在运行中 | `Promise.pending` |
| `is_settled()` | 任务已结束（成功或失败） | `Promise.settled` |
| `is_fulfilled()` | 任务成功完成 | `Promise.fulfilled` |
| `is_rejected()` | 任务失败或被取消 | `Promise.rejected` |

### 并发学习示例

```cpp
#include "flowcoro.hpp"
#include <vector>

// 学习并发处理多个任务
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

### 网络服务器学习示例

```cpp
#include "flowcoro.hpp"

// 学习实现HTTP Echo服务器
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
    
    std::cout << "学习服务器启动在 http://localhost:8080\n";
    
    // 运行事件循环
    loop_task.get();
    
    return 0;
}
```

## 📊 学习架构设计

通过这个项目学习了分层架构设计：

```
┌─────────────────────────────────┐
│        学习应用层                │  ← 业务逻辑学习
├─────────────────────────────────┤
│    网络IO层 (net.h)             │  ← 异步Socket学习  
├─────────────────────────────────┤
│   协程调度层 (core.h)           │  ← Task/Promise学习
├─────────────────────────────────┤
│  执行引擎 (thread_pool.h)       │  ← 线程池学习
├─────────────────────────────────┤
│ 数据结构 (lockfree.h)           │  ← 无锁结构学习
├─────────────────────────────────┤
│ 基础设施 (logger.h, memory.h)  │  ← 日志/内存学习
└─────────────────────────────────┘
```

### 核心组件学习

| 组件 | 文件 | 学习重点 |
|------|------|------|
| **协程核心** | `core.h` | Task、AsyncPromise、协程调度机制 |
| **数据结构** | `lockfree.h` | Queue、Stack、RingBuffer无锁实现 |
| **线程池** | `thread_pool.h` | 工作线程管理、任务调度学习 |
| **网络IO** | `net.h` | Socket、TcpServer、EventLoop学习 |
| **日志系统** | `logger.h` | 异步日志、性能统计学习 |
| **内存管理** | `memory.h` | 内存池、缓存优化学习 |

## 🎯 学习重点

### v2.2 - Task生命周期管理
- 原子状态管理 (`is_destroyed_`, `is_cancelled_`)
- 线程安全的销毁机制
- Promise风格状态查询API
- 协程句柄安全管理

### 下一步学习计划
根据[核心健壮性规划](docs/CORE_ROBUSTNESS_ROADMAP.md)：

1. **统一错误处理** - 学习Rust风格的Result<T,E>
2. **RAII资源管理** - 实现ResourceGuard和CoroutineScope
3. **调试工具** - 协程状态追踪和性能分析

## � 文档导航

### �📖 学习文档

| 文档名称 | 内容概要 |
|---------|----------|
| [API 参考](docs/API_REFERENCE.md) | 协程API接口文档与学习示例 |
| [网络编程学习](docs/NETWORK_GUIDE.md) | 异步Socket、TCP服务器学习笔记 |
| [数据库学习](docs/DATABASE_GUIDE.md) | 连接池、异步查询学习记录 |
| [RPC学习](docs/RPC_GUIDE.md) | 异步RPC、批量调用学习实践 |
| [性能学习](docs/PERFORMANCE_GUIDE.md) | 协程性能优化学习总结 |

### 📝 项目文档

| 文档名称 | 内容概要 |
|---------|----------|
| [项目总结](docs/PROJECT_SUMMARY.md) | v2.2学习成果与技术总结 |
| [技术分析](docs/TECHNICAL_SUMMARY.md) | 架构设计与实现学习记录 |
| [学习规划](docs/CORE_ROBUSTNESS_ROADMAP.md) | 后续学习计划与目标 |

## ✨ 学习重点与实现

### 🎯 **协程机制理解**

通过这个项目学习了：
- **C++20协程标准**: 基于标准协程实现，理解Promise/Awaiter机制
- **零开销抽象**: 协程创建约200ns，理解性能特点
- **异步编程模式**: 所有IO操作协程化，避免线程阻塞

### ⚡ **并发编程学习**

实践了以下概念：
- **无锁数据结构**: 队列、栈、环形缓冲区的基础实现
- **线程池理解**: 简单的工作线程管理和任务分发
- **内存管理**: 对象池、内存池的学习和简单实现

### 🌐 **网络IO学习**

学习了异步网络编程：
- **基于epoll**: Linux异步IO的基础学习
- **协程化Socket**: read/write/connect的co_await实现
- **TCP服务器**: 支持基础并发连接的学习实现

### 🗄️ **数据库异步化**

学习了异步数据库操作：
- **连接池管理**: MySQL/PostgreSQL/SQLite的基础支持
- **协程化查询**: 数据库操作支持`co_await`的学习实现
- **事务支持**: 异步事务管理的基础理解

## 🛠️ 开发记录

### v2.2.0 (当前版本)
- ✅ 增强Task生命周期管理
- ✅ 添加Promise风格状态查询API
- ✅ 实现安全销毁机制
- ✅ 清理不稳定的future组合器

### 已移除的实验性功能
- 有问题的future组合器实现
- 不稳定的测试代码
- 过于复杂的高级特性

## 🛠️ 开发工具学习

### CMake构建学习

```cmake
# 学习在项目中集成FlowCoro
find_package(FlowCoro REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE flowcoro_net)
```

### 编译选项学习

```bash
# Debug构建 (学习调试)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release构建 (性能测试)
cmake -DCMAKE_BUILD_TYPE=Release ..

# 启用内存检查 (学习内存安全)
cmake -DFLOWCORO_ENABLE_SANITIZERS=ON ..
```

### 测试和基准学习

```bash
# 运行所有测试，学习测试编写
./tests/test_core

# 简单性能测试，理解性能特点
./benchmarks/simple_benchmarks

# 网络功能测试
./examples/network_example &
curl http://localhost:8080
```

## 🚀 学习路线规划

根据[核心健壮性规划](docs/CORE_ROBUSTNESS_ROADMAP.md)，下一步学习重点：

### Phase 2.3: 核心健壮性学习 (优先级: 🔥高)

- [ ] **统一错误处理**: 学习Rust风格的Result<T,E>类型系统
- [ ] **RAII资源管理**: 学习ResourceGuard模板，CoroutineScope自动清理
- [ ] **并发安全增强**: 学习原子引用计数指针，读写锁协程化
- [ ] **调度器优化**: 学习优先级调度，负载均衡实现

### Phase 2.4: 易用性提升学习 (优先级: 🔥高)

- [ ] **链式API设计**: 学习TaskBuilder模式，Pipeline操作符
- [ ] **安全协程组合器**: 重新学习设计when_all/when_any，超时和重试机制
- [ ] **调试诊断工具**: 学习协程追踪器，性能分析器实现
- [ ] **开发者体验**: 学习更好的错误信息，调试支持

### Phase 2.5: 开发体验优化学习 (优先级: ⭐中)

- [ ] **配置系统**: 学习统一配置管理，一键式初始化
- [ ] **完善文档**: 最佳实践指南，更多实用示例
- [ ] **工具生态**: VS Code插件学习，调试器集成
- [ ] **性能监控**: 运行时指标收集，性能仪表板学习

> **学习策略**: 优先级聚焦于核心健壮性和易用性，高级特性作为长期学习目标

## 📊 当前性能

简单基准测试结果：
- 协程创建: ~200ns
- 协程切换: ~10ns  
- Task状态查询: ~5ns

> 注：这些是学习阶段的简单测试，不是严格的性能基准

## 🤔 已知问题

- 错误处理机制还比较简单
- 缺少完善的资源管理
- 调试工具不够完善
- 文档需要持续改进

## 📝 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

## 🙏 致谢

感谢以下项目提供的学习参考：
- [cppcoro](https://github.com/lewissbaker/cppcoro) - 协程库设计
- [ioManager](https://github.com/UF4007/ioManager) - 异步编程模式
- C++20标准委员会 - 协程标准化

---

*这是一个学习项目，重点在于理解协程原理而非生产使用* 📚
