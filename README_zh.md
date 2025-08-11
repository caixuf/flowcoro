# FlowCoro

高性能C++20协程库，专为批量任务处理和高吞吐量场景设计。

[English](README.md) | 中文

## 特性

- **高性能**: 189万请求/秒吞吐量，比传统多线程快137倍
- **无锁架构**: 4550万操作/秒的队列性能，智能负载均衡
- **C++20协程**: 基于现代协程的任务调度
- **批量处理**: 针对大规模并发任务执行优化
- **内存高效**: 每个任务仅271字节（包含完整调度系统）

## 性能表现

| 测试规模 | 吞吐量 | 延迟 | 相比线程 |
|----------|--------|------|----------|
| 10万任务 | 189万/秒 | 0.53μs | 快137倍 |
| 1万任务 | 47.6万/秒 | 2.1ms | 快40倍 |

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

int main() {
    sync_wait(example());
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

**不适用于:**

- 需要协程间通信的生产者-消费者模式
- 事件驱动的实时系统

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
