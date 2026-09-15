# FlowCoro

高性能C++20协程库，专为批量任务处理和高吞吐量场景设计。

[English](README.md) | 中文

## 特性

- **高性能**: 通过PGO优化实现高吞吐量场景
- **无锁架构**: 高效的队列操作和负载均衡
- **C++20协程**: 基于现代协程的任务调度
- **批量处理**: 面向并发任务执行设计
- **高级并发**: WhenAny、WhenAll等优化调度操作
- **Channel通信**: 线程安全的异步通道，支持生产者-消费者模式
- **内存池**: 参考Redis/Nginx设计的自定义内存分配
- **PGO优化**: 通过Profile-Guided编译提升性能
- **确定性实时执行**: 单线程亲和的 `RtExecutor`，面向机器人/控制/嵌入式——周期 tick、CPU 绑定、可测抖动（见 `flowcoro::rt`）。[实时控制回路示例](examples/autonomous_driving/rt_control_loop_demo.cpp)；[DDS 管道示例](examples/autonomous_driving/ad_pipeline_demo.cpp)走的是 `Task<>`，不是 `RtExecutor`。
- **有界 MPMC 无锁通道**: `BoundedChannel<T>`（Vyukov 环，无分配、无 SMR、满/空立即返回），补齐 `Channel<T>` 只服务协程的空缺
- **CPU 亲和性**: `cpu_affinity.h` 统一绑核与**物理核**枚举（按 `thread_siblings_list` 去重，SMT 兄弟不会分给两个 worker），`lockfree::ThreadPool` 可直接绑核
- **Python 绑定（可选）**: `-DFLOWCORO_BUILD_PYTHON=ON` 产出 `flowcoro_py.so`——`CoroutineThreadPool` + `when_all`/`wait_any` + `Channel`，见 [Python 绑定文档](docs/PYTHON_BINDING.md)

## 性能表现

具体数字以 [性能数据参考](docs/PERFORMANCE_DATA.md) 为准。下面摘自作者机器 **2026-09-15** 的一次 Release 跑（`professional_flowcoro_benchmark`：i7-14650HX 6C/12T，Ubuntu 24.04.4 LTS WSL2，g++ 13.3.0，FlowCoro 4.0.0，`thread_count=12`）。这不是「已在超大规模生产验证」的声明。

| Benchmark | Mean | Throughput |
|-----------|------|------------|
| Simple Computation | 14 ns | 69.0M ops/s |
| Coroutine Create & Execute | 132 ns | 7.60M ops/s |
| WhenAny (2 tasks) | 630 ns | 1.59M ops/s |
| LockFree Queue (enq+deq) | 132 ns | 7.56M ops/s |
| Memory Allocation (1KB) | 24 ns | 41.3M ops/s |

部分行（HTTP、Echo）是 CPU 侧模拟，不是真实套接字。本次 **没有** 刷新 Go 数字；同机有一份 Rust 微基准，但方法不完全相同——请看 PERFORMANCE_DATA.md，不要用「比 Go/Rust 快 N 倍」来概括。

**适用场景**: 协程调度、批量/高吞吐，以及 `flowcoro::rt` 确定性实时路径。

## 快速开始

### 环境要求

- C++20编译器 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### 构建

```bash
git clone https://github.com/caixuf/flowcoro.git
cd flowcoro
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**注意**: 当`pgo_profiles/`目录中存在配置文件数据时，会自动启用PGO优化。

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
    // 任务创建时开始同步执行，遇到挂起点后进入调度系统
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

- **suspend_never**: 任务创建时同步执行直到首个co_await挂起点
- **负载均衡**: 协程挂起后自动选择调度器
- **无锁队列**: 高性能任务分发
- **延续机制**: 零拷贝任务链接

### 确定性实时执行（`flowcoro::rt`）

在高吞吐调度器之外，FlowCoro 附带一个**单线程确定性实时执行模型**（`RtExecutor`），面向延迟敏感的控制回路（机器人/自动驾驶/嵌入式）：

- **单线程亲和**：所有 `resume`/`destroy` 都在执行器线程上发生；跨线程事件只通过 `post_ready(h)` 递回句柄，绝不 inline `resume`。这正是 FlowEngine 依赖它避免数据竞争的原因。
- **周期 tick**：`run()` 是非阻塞 tick。`rt::sleep_until` 对齐绝对周期；`rt::yield()` 推迟到下一 tick。
- **CPU 绑定**：`Config{.pin_cpu = N}` 在第一次 `run()` 时绑定**当前**线程（`apply_affinity()`，Linux）。
- **本地热路径**：内部重投递走预留的线程私有 vector。`post_ready` 会分配队列节点；`run_blocking` 空闲时可能 `sleep_until`。不要理解成处处零 syscall。
- **两段式关停**：`request_stop()` → 周期边界协作式停止 → 帧 `co_return` 并在执行器线程销毁。

```cpp
#include <flowcoro/rt_executor.h>

flowcoro::rt::RtExecutor ex{{ .pin_cpu = 2 }};
ex.spawn(periodic_control_task(), "control");
ex.run_blocking();   // 周期 tick 直到所有任务结束
// 或: while (!ex.is_finished()) ex.run();
```

完整用法见 [API 参考 §9](docs/API_REFERENCE.md#9-确定性实时执行-rtexecutor)、抖动灯笼 `tests/test_rt_latency.cpp` 与[实时控制回路示例](examples/autonomous_driving/rt_control_loop_demo.cpp)。[DDS 管道示例](examples/autonomous_driving/ad_pipeline_demo.cpp)是 Task+Channel 中间件，不在 `RtExecutor` 上跑。

## 使用场景

**适用于:**

- Web API服务器 (高请求吞吐量)
- 批量数据处理
- 高频交易系统
- 微服务网关
- 生产者-消费者管道
- 多阶段数据处理工作流

**配合实时层（`flowcoro::rt`）:**

- 机器人/自动驾驶控制回路（周期、延迟敏感）
- AMR/无人机稳控与导航
- 嵌入式确定性调度
- 任何需要单线程亲和 + CPU 绑定的嵌入式/控制负载

试试[实时控制回路示例](examples/autonomous_driving/rt_control_loop_demo.cpp)：

```bash
cd build && cmake .. && cmake --build . --target rt_control_loop_demo
./examples/autonomous_driving/rt_control_loop_demo 2
```

**Channel增强特性:**

- 协程间通信
- 缓冲消息传递
- 异步数据管道
- 协调多生产者/多消费者系统

## 性能测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target professional_flowcoro_benchmark -j$(nproc)
./build/benchmarks/professional_flowcoro_benchmark

# 可选：不同规模（与 professional 基准不是同一套方法）
./build/examples/hello_world 10000   # 1万任务
./build/examples/hello_world 100000  # 10万任务
```

## 文档

- [FlowCoro 介绍](docs/INTRODUCTION.md)
- [快速开始指南](docs/QUICK_START.md)
- [API参考](docs/API_REFERENCE.md)
- [架构设计](docs/ARCHITECTURE.md)
- [性能数据](docs/PERFORMANCE_DATA.md)
- [PGO优化指南](docs/PGO_GUIDE.md)
- [Python 绑定](docs/PYTHON_BINDING.md)

## 许可证

MIT许可证 - 详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎贡献代码！请阅读我们的贡献指南并提交Pull Request。
