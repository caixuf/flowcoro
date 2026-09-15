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

### 最新专业基准测试结果 (16核Linux系统)

#### 核心性能指标对比

| 性能指标 | FlowCoro | Go | Rust | 相对表现 |
|----------|----------|-----|------|----------|
| **协程/任务创建和执行** | 4.20M ops/s | 2.27M ops/s | 19.5K ops/s | 比Go快1.85倍，比Rust快215倍 |
| **通道/队列操作** | 9.61M ops/s | 11.59M ops/s | 9.15M ops/s | 与Go差17%，比Rust快5% |
| **HTTP请求处理** | 36.77M ops/s | 41.99M ops/s | 45.42M ops/s | 达到行业水平，差距12-19% |
| **简单计算** | 46.19M ops/s | 21.82M ops/s | 46.34M ops/s | 与Rust基本相当 |
| **内存分配(1KB)** | 10.18M ops/s | 41.43M ops/s | 46.75M ops/s | 有改进空间 |

#### 关键发现

**FlowCoro优势领域:**
- 协程创建和执行性能显著领先
- 专业的WhenAny/WhenAll并发控制特性
- 在复杂协程调度场景中表现优秀

**需要改进的领域:**
- 内存分配性能相对较弱
- 简单通道操作有优化空间

**整体评估:**
FlowCoro在核心协程功能上表现出色，特别适合复杂的并发控制场景。在基础性能指标上已达到行业竞争水平，在专业协程调度方面具有明显优势。

### 性能分析

**Go的优势：**
- 轻量级goroutine，在通道操作中表现优秀(11.59M ops/s)
- 成熟的运行时优化，HTTP处理性能领先(41.99M ops/s)
- 内存分配效率高，达到41.43M ops/s

**FlowCoro的特点：**
- 协程创建和执行性能显著领先(4.20M ops/s vs Go的2.27M ops/s)
- 专业的WhenAny/WhenAll并发控制特性
- 在复杂协程调度场景中具有优势

**Rust的特色：**
- 简单计算性能最优(46.34M ops/s)
- HTTP处理效率最高(45.42M ops/s)
- 内存安全保证，在基础操作上表现出色

## 测试设计

所有测试都采用相同的设计原则：
- 去除IO延迟，专注测试协程调度性能
- 同机器同环境，确保公平对比
- 统一的输出格式，便于结果对比

## 实际测试结果 (10,000并发请求)

### 系统配置

- CPU：16核心
- 操作系统：Linux
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

**专业基准测试 (单操作性能)**：
- 更准确反映各语言在协程核心操作上的性能
- FlowCoro在协程创建执行方面领先
- Go在内存分配和HTTP处理方面表现优秀
- Rust在计算密集型任务中表现最佳

**历史批量测试 (任务规模性能)**：
- 反映大规模并发任务的整体处理能力
- Go在大规模批量处理中优势明显
- 测试方法与现代基准测试不同，结果不可直接对比

### 结论

- **复杂协程调度**：FlowCoro表现最佳，特别是在协程创建执行方面
- **高并发HTTP服务**：Rust和Go表现优秀，差距较小
- **内存敏感场景**：Go和Rust在内存分配方面领先
- **计算密集型任务**：Rust和FlowCoro性能相当，显著优于Go

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
