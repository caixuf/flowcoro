# FlowCoro

高性能C++20协程库，专为批量任务处理和高吞吐量场景设计。

[English](README.md) | 中文

## 特性

- **高性能**: 420万次/秒协程创建和执行性能
- **无锁架构**: 961万次/秒队列操作，智能负载均衡
- **C++20协程**: 基于现代协程的任务调度
- **批量处理**: 针对大规模并发任务执行优化
- **高级并发**: WhenAny、WhenAll等专业调度特性
- **Channel通信**: 线程安全的异步通道，支持生产者-消费者模式

## 性能表现

### 核心性能对比

| 性能指标 | FlowCoro | Go | Rust | 说明 |
|----------|----------|-----|------|------|
| **协程创建和执行** | 420万次/秒 | 227万次/秒 | 1.95万次/秒 | 比Go快1.85倍 |
| **无锁队列** | 961万次/秒 | 1159万次/秒 | 915万次/秒 | 具有竞争力 |
| **HTTP请求处理** | 3677万次/秒 | 4199万次/秒 | 4542万次/秒 | 行业级性能 |
| **简单计算** | 4619万次/秒 | 2182万次/秒 | 4634万次/秒 | 与Rust相当 |
| **内存分配(1KB)** | 1018万次/秒 | 4143万次/秒 | 4675万次/秒 | 有改进空间 |

**专业特长**: 复杂协程调度、批量处理、并发任务管理

## 快速开始

### 环境要求

- C++20编译器 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### 构建

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 基本用法

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 简单协程任务
Task<int> compute(int value) {
    // 模拟工作
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

// 并发执行
Task<void> example() {
    // 任务创建时立即开始执行
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
    // 等待结果
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
    
    std::cout << "结果: " << result1 << ", " << result2 << ", " << result3 << std::endl;
    co_return;
}

// 使用Channel的生产者-消费者模式
Task<void> channel_example() {
    auto channel = make_channel<int>(10); // 缓冲区大小: 10
    
    // 生产者
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await channel->send(i);
            std::cout << "生产: " << i << std::endl;
        }
        channel->close();
    };
    
    // 消费者
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break; // Channel已关闭
            std::cout << "消费: " << value.value() << std::endl;
        }
    };
    
    auto prod_task = producer();
    auto cons_task = consumer();
    
    co_await prod_task;
    co_await cons_task;
    co_return;
}

int main() {
    sync_wait(example());
    sync_wait(channel_example());
    return 0;
}
```

## 架构设计

FlowCoro采用三层调度架构：

```text
任务创建 → 协程管理器 → 协程池 → 线程池
   ↓         ↓        ↓      ↓
suspend_never  负载均衡  无锁队列  执行
```

- **suspend_never**: 任务创建时立即执行
- **智能负载均衡**: 自动调度器选择
- **无锁队列**: 高性能任务分发
- **延续机制**: 零拷贝任务链接

## 使用场景

**适用于:**

- Web API服务器 (50万+请求/秒)
- 批量数据处理
- 高频交易系统
- 微服务网关
- 生产者-消费者管道
- 多阶段数据处理工作流

**Channel增强特性:**

- 协程间通信
- 缓冲消息传递
- 异步数据管道
- 协调多生产者/多消费者系统

## 性能测试

```bash
# 运行性能测试
./build/benchmarks/simple_benchmark

# 测试不同规模
./build/examples/hello_world 10000   # 1万任务
./build/examples/hello_world 100000  # 10万任务
```

## 文档

- [API参考](docs/API_REFERENCE.md)
- [协程调度器](docs/COROUTINE_SCHEDULER.md)
- [使用指南](docs/USAGE.md)
- [任务执行流程](docs/TASK_EXECUTION_FLOW.md)

## 许可证

MIT许可证 - 详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎贡献代码！请阅读我们的贡献指南并提交Pull Request。
