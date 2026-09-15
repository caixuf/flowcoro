# 跨语言性能对比测试指南

本目录包含FlowCoro与Go、Rust的真实性能对比测试代码。

## 诚实的真实性能测量（请先读）

`professional_flowcoro_benchmark` 里名叫 **Echo Server / Concurrent Echo / HTTP Request Processing / Memory Pool** 的行是 **CPU-sim**（算循环 / `strlen` / `malloc`），**没有** `socket`/`accept`/`read`/`write`，不能当成 HTTP QPS 或网卡吞吐。下面两个二进制才是本目录的真实测量：

| 二进制 | 测什么 | 不是什么 |
|--------|--------|----------|
| `real_net_benchmark` | localhost TCP echo：真实 `flowcoro::net`（Linux epoll / Windows WSAPoll） | 不是网卡/线缆；不是 HTTP |
| `rt_cycle_jitter_benchmark` | `RtExecutor` 控制回路周期误差 / tardiness p50/p99/max | 不是硬实时证书 |

### 真实套接字 IO

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target real_net_benchmark -j$(nproc)
./build/benchmarks/real_net_benchmark
# 或单独场景:
./build/benchmarks/real_net_benchmark echo --duration-ms 2000 --clients 8 --payload 64
./build/benchmarks/real_net_benchmark connect --connections 200 --payload 64
```

环境变量：`FLOWCORO_REAL_NET_DURATION_MS` / `CLIENTS` / `PAYLOAD` / `CONNECTIONS` / `PORT`。

怎么读输出：

- 标题写明 **TCP 127.0.0.1 kernel loopback**。这是本机回环，不是 NIC。
- `req/s` / `conn/s` 以及 RTT 的 p50/p95/p99/max（微秒）。
- 不要拿这些数字去除以 Go/Rust 再写「快 X 倍」——本程序不打印对照。
- 已知缺口：每次 `Socket::read`/`write` 会 `add_fd`/`remove_fd`（one-shot epoll），所以这是当前栈的真实成本，不是调优后的上限。

CI 冒烟：`test_real_net_echo`（少量 round-trip，不断言吞吐）。

### FlowEngine 式周期抖动

```bash
cmake --build build --target rt_cycle_jitter_benchmark test_rt_cycle_jitter -j$(nproc)
./build/benchmarks/rt_cycle_jitter_benchmark
FLOWCORO_RT_SLO_STRICT=1 ./build/benchmarks/rt_cycle_jitter_benchmark --period-us 10000 --duration-ms 2000
ctest --test-dir build -R test_rt_cycle_jitter --output-on-failure
```

三条路径：

1. **host-tick + yield**：宿主 `sleep_until` 对齐绝对周期，任务 `co_await yield()`。
2. **sleep_for**：相对睡眠，相对绝对 deadline 会漂移；宿主约 200µs 轮询 `run()`（当前 main 没有 `next_timer_deadline`；PR #20 若合入会更准）。
3. **sensor `post_ready`**：生产者线程按周期 `post_ready`，执行器忙等 `run()`。

报告 **interval**（相邻 tick 间隔）和 **late**（`max(0, now - deadline)`）的 p50/p99/max。`FLOWCORO_RT_SLO_STRICT=1` 按 10ms 周期打印本机闸门（p99 late ≤ 3ms），bench 不因此失败。CI 测试用宽松阈值（p99 late ≤ 200ms）。

---

## 快速开始


### 环境要求
- Go 1.20+
- Rust 1.70+ (with Cargo)
- FlowCoro已编译完成

### 运行对比测试

```bash
# 1. 编译所有测试程序
make -C ../build -j$(nproc) # 编译FlowCoro
go build -o go_benchmark go_benchmark.go # 编译Go版本
cargo build --release # 编译Rust版本

# 2. 运行10,000并发任务对比测试
echo "=== FlowCoro测试 ==="
../build/examples/hello_world_concurrent coroutine 10000

echo "=== Go测试 ==="
./go_benchmark 10000

echo "=== Rust测试 ==="
./target/release/rust_benchmark 10000
```

## 测试结果解读

### 关键指标
- **总耗时**: 完成所有任务的总时间
- **吞吐量**: 每秒处理的任务数 (req/sec)
- **内存增长**: 测试过程中的内存使用增量
- **单任务内存**: 平均每个任务的内存开销

### 最新专业基准测试结果（2026-09-15）

环境（作者机器，与 [docs/PERFORMANCE_DATA.md](../docs/PERFORMANCE_DATA.md) 相同）：

- CPU：Intel Core i7-14650HX（6 cores / 12 threads），**不是 16 核**
- OS：Ubuntu 24.04.4 LTS（WSL2），Linux 6.6.x microsoft-standard-WSL2
- 编译器：g++ 13.3.0；构建：Release，FlowCoro 4.0.0，`thread_count=12`
- Go：**本次未跑**，下表没有 Go 列，也没有「比 Go 快 X 倍」

复现：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target professional_flowcoro_benchmark -j$(nproc)
./build/benchmarks/professional_flowcoro_benchmark
```

#### FlowCoro（`professional_flowcoro_benchmark`）

| Benchmark | Mean | Throughput |
|-----------|------|------------|
| Simple Computation | 14 ns | 69.0M ops/s |
| Coroutine Creation Only | 128 ns | 7.81M ops/s |
| Coroutine Create & Execute | 132 ns | 7.60M ops/s |
| Void Coroutine | 111 ns | 9.01M ops/s |
| WhenAny (2 tasks) | 630 ns | 1.59M ops/s |
| WhenAny 4 Tasks | 1513 ns | 0.66M ops/s |
| LockFree Queue (enq+deq) | 132 ns | 7.56M ops/s |
| Memory Allocation (1KB) | 24 ns | 41.3M ops/s |
| Memory Pool Allocation (1KB) | 15 ns | 68.3M ops/s |
| Echo Server Throughput | 34 ns | 29.2M ops/s |
| Concurrent Echo Clients | 16771 ns | 59.6K ops/s |
| HTTP Request Processing | 16 ns | 62.2M ops/s |
| Concurrent Task Processing | 903 ns | 1.11M ops/s |
| Sleep 1us | 129 ns | 7.73M ops/s |

Echo / HTTP 等行是 CPU 侧模拟（无真实套接字 IO）；「Memory Pool Allocation」当前实现是 `malloc`/`free`。完整说明见 PERFORMANCE_DATA.md。

#### 同机 Rust（可选，方法不完全相同）

同机跑了 `professional_rust_benchmark`（rustc 1.96.0）。近似吞吐：**不要**与上表逐行当作同一方法：

| Rust bench | 约 Throughput |
|------------|----------------|
| Task create+exec | 29.5K ops/s |
| Channel | 6.04M ops/s |
| Simple compute | 72.7M ops/s |
| Concurrent tasks (10) | 867 ops/s |
| Mem alloc 1KB | 68.2M ops/s |
| Concurrent echo | 809 ops/s |
| HTTP | 71.4M ops/s |

较接近的形状：简单计算（69.0M vs 约 72.7M）、1KB 分配（41.3M vs 约 68.2M）。任务创建（7.60M vs 约 29.5K）不可比：FlowCoro 是 `suspend_never` 的 `Task`，Rust 是 `tokio::spawn` + await。

旧的「16 核 / 4.20M 创建 / 比 Go 快 1.85 倍」表已从本文当前结果中撤下，见 PERFORMANCE_DATA.md 的历史隔离段。

## 测试设计

`professional_*` 程序意图对齐输出格式，并尽量去掉真实 IO。对照源码后：**并非每一行测的是同一件事**（例如 HTTP/Echo 多为 CPU 模拟；Rust 任务创建走 `tokio::spawn`，FlowCoro 走 `suspend_never` 的 `Task`）。跨语言倍数只应在确认源码形状一致、且同机同次跑过之后再写。本次没有 Go 结果。

## 实际测试结果 (10,000并发请求)

### 系统配置（历史 10K 批量测试当时记录，不是 2026-09-15 这次）

- 当时文档写的是「CPU：16 核心 / Linux」；**未与本次 i7-14650HX 6C/12T WSL2 跑对齐，也未复现**
### 历史测试数据

#### 10K任务规模测试 (历史参考)

以下为早期基于任务规模的测试结果，供历史参考：

| 语言/框架 | 总耗时 | 吞吐量 | 内存使用变化 | 单请求内存开销 |
|-----------|--------|--------|--------------|----------------|
| **Go Goroutines** | 4ms | 2,500,000 req/sec | +408KB | 41 bytes |
| **Rust Tokio** | 6ms | 1,666,666 req/sec | +0KB | 0 bytes |
| **C++ FlowCoro** | 1.3ms | 7,727,975 ops/sec | - | - |

注：该测试基于任务批量处理，与上述专业基准测试的单操作性能测试方法不同。

### 详细结果

#### Go Goroutines

```text
总请求数: 10000 个
总耗时: 4 ms
平均耗时: 0.0004 ms/请求
吞吐量: 2500000 请求/秒
内存变化: 185 KB → 593 KB (增加 408 KB)
单请求内存: 41 bytes/请求
```

#### Rust Tokio

```text
总请求数: 10000 个
总耗时: 6 ms
平均耗时: 0.0007 ms/请求
吞吐量: 1666666 请求/秒
内存变化: 750480 KB → 750480 KB (增加 0 KB)
单请求内存: 0 bytes/请求
```

#### C++ FlowCoro

```text
Basic Coroutine: 10000 次 | 1.294ms | 平均 0.000129ms | 7727975.27 ops/sec
Coroutine Scheduling: 1000 次 | 0.143ms | 平均 0.000143ms | 6993006.99 ops/sec
Concurrent Coroutines: 100 次 | 127.076ms | 平均 1.270760ms | 786.93 ops/sec
```

### 历史批量任务性能分析 (基于10K任务测试)

1. **Go优势**：
   - 批量任务处理高效，4ms完成10000个并发任务
   - 内存使用合理，每个goroutine仅占41字节
   - M:N调度器在大规模并发中表现稳定

2. **Rust优势**：
   - 零成本抽象，批量处理时无额外内存分配
   - 编译时优化充分
   - 内存安全保证

3. **C++ FlowCoro优势**：
   - 单个协程操作性能在历史测试中表现优秀（770万ops/sec）
   - 底层控制能力强
   - 可根据需求定制调度策略

### 现代基准测试 vs 历史批量测试

**专业基准测试（单操作均值，`professional_*`）**：
- 当前 FlowCoro 数字以 2026-09-15 作者机器那次 Release 跑为准（上表 / PERFORMANCE_DATA.md）
- 本次 **没有** 刷新的 Go 数字；同机 Rust 数字方法不完全相同，见上
- 若干行（HTTP、Echo）不是真实网络服务

**历史批量测试（任务规模）**：
- 反映大规模并发任务的整体处理能力
- 与专业基准的单操作均值 **不可直接对比**

## 文件说明

- `professional_go_benchmark.go`: Go专业基准测试
- `professional_rust_benchmark/`: Rust专业基准测试项目
- `professional_flowcoro_benchmark.cpp`: FlowCoro专业基准测试
- `go_benchmark.go`: Go历史批量测试 (历史参考)
- `rust_benchmark.rs`: Rust历史批量测试 (历史参考)
- `Cargo.toml`: Rust项目配置

## 注意事项

- 编译产物已被.gitignore忽略
- 首次运行Rust测试需要下载依赖包
- 测试结果可能因硬件环境略有差异
