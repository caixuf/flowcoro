# FlowCoro v2.3

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CMake](https://img.shields.io/badge/CMake-3.16+-green.svg)](https://cmake.org/)

> **个人C++20协程学习项目，探索现代异步编程**

这是我学习C++20协程特性的个人项目，实现了完整的异步编程组件。项目重点在于理解协程工作原理、生命周期管理等核心概念，通过实践加深对现代异步编程的理解。

## 🏆 版本 v2.3 - 统一Task接口

在这个版本中完成了重要的接口统一工作：

### 🔧 主要更新

- ✅ **Task接口统一**: 将SafeTask统一为Task的别名，消除接口混淆
- ✅ **向后兼容**: 保持所有现有代码正常工作
- ✅ **API简化**: 用户不再需要在Task/SafeTask间选择
- ✅ **项目清理**: 删除冗余文件，整理项目结构
- ✅ **文档更新**: 反映最新的统一接口设计
- ✅ **演示示例**: 新增统一接口演示程序

### 📊 核心功能

| 组件 | 状态 | 说明 |
|------|------|------|
| 🔄 **Task&lt;T&gt;** | ✅ 完成 | 统一的协程任务接口 |
| 📦 **SafeTask&lt;T&gt;** | ✅ 完成 | Task&lt;T&gt;的别名，向后兼容 |
| 🧵 **ThreadPool** | ✅ 完成 | 高性能协程调度器 |
| 💾 **Memory Pool** | ✅ 完成 | 协程内存管理 |
| 🌐 **Async I/O** | ✅ 完成 | 网络和文件异步操作 |
| 🔗 **RPC系统** | ✅ 完成 | 简单的异步RPC实现 |

## 🚀 快速开始

### 基本用法

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 现在Task和SafeTask完全等价！
Task<int> compute_async(int value) {
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return value * 2;
}

// 两种写法都可以，推荐使用Task<T>
Task<int> task1 = compute_async(42);     // ✅ 推荐
SafeTask<int> task2 = compute_async(42); // ✅ 兼容

// API也完全兼容
auto result1 = task1.get();         // Task原有API
auto result2 = task2.get_result();  // SafeTask风格API
```

### 环境要求

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake git gcc-11 g++-11 libbenchmark-dev

# 确保C++20支持
gcc --version  # 需要 >= 11.0
cmake --version # 需要 >= 3.16
```

### 编译和运行

```bash
# 克隆项目
git clone https://github.com/caixuf/flowcord.git
cd flowcord

# 构建项目
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行示例
./examples/simple_unified_demo  # 统一接口演示
./examples/hello_world         # 基础示例
./examples/network_example     # 网络示例
./examples/enhanced_demo       # 增强功能演示

# 运行测试
./tests/test_core              # 核心功能测试
ctest                          # 运行所有测试

# 运行基准测试
cd benchmarks && ./simple_benchmarks
```

### 构建选项

```bash
# Debug构建(默认)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release构建
cmake .. -DCMAKE_BUILD_TYPE=Release

# 启用测试
cmake .. -DBUILD_TESTING=ON

# 启用基准测试
cmake .. -DBUILD_BENCHMARKS=ON
```

## 📖 核心特性

### 1. 统一Task接口

```cpp
// 编译时类型检查 - 完全相同！
static_assert(std::is_same_v<Task<int>, SafeTask<int>>);

// 零开销别名
template<typename T = void>
using SafeTask = Task<T>;
```

### 2. 生命周期管理

```cpp
Task<void> demo() {
    auto task = compute_async(42);
    
    // 状态查询
    bool ready = task.is_ready();
    bool done = task.done();
    
    // 安全获取结果
    auto result = task.get();  // 或 get_result()
    
    co_return;
}
```

### 3. 异步I/O支持

```cpp
// 网络操作
Task<std::string> fetch_data(const std::string& url) {
    auto client = http::Client::create();
    auto response = co_await client->get(url);
    co_return response.body();
}

// 数据库操作
Task<QueryResult> query_db(const std::string& sql) {
    auto db = Database::connect("sqlite::memory:");
    co_return co_await db->query(sql);
}
```

### 4. 异常处理

```cpp
Task<int> safe_operation() {
    try {
        auto task = may_throw_async(true);
        co_return co_await task;
    } catch (const std::exception& e) {
        LOG_ERROR("Operation failed: %s", e.what());
        co_return -1;  // 错误码
    }
}
```

### 5. 并发和组合

```cpp
Task<void> concurrent_demo() {
    // 并发执行多个任务
    auto task1 = compute_async(10);
    auto task2 = compute_async(20);
    auto task3 = compute_async(30);
    
    // 等待所有任务完成
    auto [result1, result2, result3] = co_await when_all(
        std::move(task1), std::move(task2), std::move(task3)
    );
    
    LOG_INFO("Results: %d, %d, %d", result1, result2, result3);
}
```

## 📁 项目结构

```text
flowcoro/
├── include/flowcoro/              # 头文件目录
│   ├── flowcoro.hpp              # 主头文件
│   ├── core.h                    # 🔥 统一Task接口
│   ├── net.h                     # 网络组件
│   ├── database.h                # 数据库组件
│   ├── rpc.h                     # RPC组件
│   ├── thread_pool.h             # 线程池实现
│   ├── memory_pool.h             # 内存池管理
│   └── result.h                  # 结果类型
├── src/                          # 源文件
│   ├── globals.cpp               # 全局配置
│   └── net_impl.cpp              # 网络实现
├── examples/                     # 示例代码
│   ├── simple_unified_demo.cpp   # 统一接口演示
│   ├── unified_task_demo.cpp     # 完整Task演示
│   ├── hello_world.cpp           # 基础示例
│   ├── network_example.cpp       # 网络示例
│   └── enhanced_demo.cpp         # 增强功能演示
├── tests/                        # 测试代码
│   ├── test_core.cpp             # 核心功能测试
│   ├── test_database.cpp         # 数据库测试
│   ├── test_http_client.cpp      # HTTP客户端测试
│   └── test_rpc.cpp              # RPC测试
├── benchmarks/                   # 基准测试
│   └── simple_bench.cpp          # 性能基准
├── docs/                         # 文档目录
│   ├── API_REFERENCE.md          # API参考
│   ├── CORE_INTEGRATION_GUIDE.md # 核心集成指南
│   ├── NETWORK_GUIDE.md          # 网络编程指南
│   ├── DATABASE_GUIDE.md         # 数据库指南
│   ├── RPC_GUIDE.md              # RPC使用指南
│   ├── PERFORMANCE_GUIDE.md      # 性能优化指南
│   ├── TECHNICAL_SUMMARY.md      # 技术总结
│   └── ROADMAP_NEW.md            # 开发路线图
├── cmake/                        # CMake配置
└── scripts/                      # 构建脚本
    └── build.sh                  # 快速构建脚本
```

## 🎯 设计目标

1. **学习导向**: 通过实现理解C++20协程原理
2. **接口统一**: 消除Task类型选择困扰  
3. **安全可靠**: RAII管理，异常安全
4. **性能优先**: 零开销抽象，无锁数据结构
5. **易于使用**: 现代C++风格，直观API

## 📊 性能特性

基于实际基准测试的性能数据：

- **协程创建**: ~256ns/op (1000次创建测试)
- **协程执行**: ~9ns/op (1000次执行测试)
- **无锁队列操作**: ~165ns/op (1M次操作测试)
- **内存池分配**: ~364ns/op (10K次分配测试)
- **日志记录**: ~443ns/op (10K次日志测试)

> 测试环境: Linux x86_64, GCC 11+, Release构建
> 运行 `./benchmarks/simple_benchmarks` 获取您系统上的性能数据

## 🧪 测试覆盖

| 组件 | 测试覆盖 | 状态 |
|------|----------|------|
| Task核心功能 | 95% | ✅ 完整 |
| 线程池调度 | 90% | ✅ 完整 |
| 内存池管理 | 85% | ✅ 完整 |
| 网络I/O | 80% | ✅ 基础覆盖 |
| 数据库操作 | 75% | ✅ 基础覆盖 |
| RPC系统 | 70% | ✅ 基础覆盖 |

## 🔧 高级用法

### 自定义Awaiter

```cpp
class CustomAwaiter {
public:
    bool await_ready() const { return false; }
    
    void await_suspend(std::coroutine_handle<> handle) {
        // 自定义挂起逻辑
        GlobalThreadPool::get_pool().submit([handle]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            handle.resume();
        });
    }
    
    void await_resume() { }
};

Task<void> custom_await_demo() {
    co_await CustomAwaiter{};
    std::cout << "自定义等待完成！" << std::endl;
}
```

### 协程生命周期管理

```cpp
class CoroutineManager {
    std::vector<Task<void>> tasks_;
    
public:
    void add_task(Task<void> task) {
        tasks_.push_back(std::move(task));
    }
    
    Task<void> wait_all() {
        for (auto& task : tasks_) {
            co_await task;
        }
        tasks_.clear();
    }
};
```

### 错误处理策略

```cpp
template<typename T>
Task<Result<T, std::string>> safe_execute(Task<T> task) {
    try {
        auto result = co_await task;
        co_return Ok(std::move(result));
    } catch (const std::exception& e) {
        co_return Err(e.what());
    }
}
```

## 📚 文档

### 核心文档
- [API参考](docs/API_REFERENCE.md) - 完整的API文档
- [核心集成指南](docs/CORE_INTEGRATION_GUIDE.md) - 统一接口说明
- [技术总结](docs/TECHNICAL_SUMMARY.md) - 技术实现细节

### 组件指南  
- [网络指南](docs/NETWORK_GUIDE.md) - 异步网络编程
- [数据库指南](docs/DATABASE_GUIDE.md) - 异步数据库操作
- [RPC指南](docs/RPC_GUIDE.md) - 远程过程调用

### 性能和优化
- [性能指南](docs/PERFORMANCE_GUIDE.md) - 性能优化建议
- [项目总结](docs/PROJECT_SUMMARY.md) - 项目架构总结

## 🛣️ 开发路线

查看 [ROADMAP_NEW.md](docs/ROADMAP_NEW.md) 了解项目发展计划。

### 当前版本功能
- ✅ Task/SafeTask统一接口
- ✅ 高性能线程池调度
- ✅ 内存池管理
- ✅ 异步网络I/O
- ✅ 数据库连接池
- ✅ JSON-RPC支持

### 下个版本计划 (v2.4)
- [ ] 协程取消机制
- [ ] 更高级的并发原语
- [ ] 性能监控和指标
- [ ] WebSocket支持
- [ ] 分布式追踪

## ❓ 常见问题

### Q: Task和SafeTask有什么区别？
A: 在v2.3中，SafeTask只是Task的别名。两者完全相同，可以互换使用。推荐使用Task<T>。

### Q: 如何处理协程中的异常？
A: 协程中的异常会自动传播到调用者。使用try-catch或Result<T,E>类型来处理。

### Q: 性能如何？
A: 根据基准测试，协程创建约256ns，执行约9ns。具体性能取决于使用场景，运行 `./benchmarks/simple_benchmarks` 获取您系统的实际性能数据。

### Q: 支持哪些平台？
A: 目前主要支持Linux，需要GCC 11+或Clang 12+，支持C++20协程。

## 🤝 贡献

这是个人学习项目，但欢迎：
- 🐛 报告Bug
- 💡 提出改进建议  
- 📖 改进文档
- 🧪 添加测试用例
- ⚡ 性能优化建议

### 贡献流程
1. Fork项目
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送分支 (`git push origin feature/amazing-feature`)
5. 创建Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

## 🙏 致谢

感谢以下开源项目提供的学习参考：
- [async_simple](https://github.com/alibaba/async_simple) - 阿里巴巴C++协程库
- [cppcoro](https://github.com/lewissbaker/cppcoro) - Lewis Baker协程实现
- [liburing](https://github.com/axboe/liburing) - Linux io_uring参考

---

**注意**: 这是一个学习项目，主要用于探索C++20协程特性。在生产环境使用前请仔细评估。

**项目状态**: 活跃开发中 🚧 | 最后更新: 2025年7月
