# FlowCoro 快速开始指南

高性能C++20协程库，专为批量任务处理和高吞吐量场景设计。

## 环境要求

- C++20编译器 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

## 安装构建

```bash
git clone https://github.com/caixuf/flowcoro.git
cd flowcoro
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 基本使用

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 简单协程任务
Task<int> compute(int value) {
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

// 并发执行
Task<void> example() {
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
    
    std::cout << "Results: " << result1 << ", " << result2 << ", " << result3 << std::endl;
    co_return;
}

int main() {
    sync_wait(example());
    return 0;
}
```

## Channel通信示例

```cpp
Task<void> channel_example() {
    auto channel = make_channel<int>(10);
    
    // 生产者
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await channel->send(i);
        }
        channel->close();
    };
    
    // 消费者
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            std::cout << "Received: " << value.value() << std::endl;
        }
    };
    
    auto prod_task = producer();
    auto cons_task = consumer();
    
    co_await prod_task;
    co_await cons_task;
    co_return;
}
```

## 运行测试

```bash
# 基本功能测试
./build/tests/test_core

# 性能测试
./build/benchmarks/professional_flowcoro_benchmark

# 示例程序
./build/examples/hello_world 10000
```

## 进一步学习

- [完整API参考](API_REFERENCE.md) - 详细的API文档和使用示例
- [架构说明](ARCHITECTURE.md) - 核心架构和调度机制
- [性能数据](PERFORMANCE_DATA.md) - 性能基准和对比
- [PGO优化](PGO_GUIDE.md) - Profile-Guided Optimization指南

## 核心特性

- **高性能**: PGO优化后达到百万级请求/秒
- **无锁架构**: 高吞吐量队列操作
- **C++20协程**: 现代协程调度系统
- **Channel通信**: 线程安全的异步通道
- **内存池**: 高性能内存分配
