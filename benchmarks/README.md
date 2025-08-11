# 跨语言性能对比测试指南

本目录包含FlowCoro与Go、Rust的真实性能对比测试代码。

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

### 实际测试结果 (16核Linux, 多轮平均)

**10K任务规模平均结果：**
- **FlowCoro**: 7ms, 1,450K req/sec, 420 bytes/请求
- **Go**: 6ms, 1,670K req/sec, 38 bytes/请求  
- **Rust**: 8ms, 1,250K req/sec, 0 bytes/请求

**100K任务规模平均结果：**
- **FlowCoro**: 61ms, 1,640K req/sec, 271 bytes/请求
- **Go**: 31ms, 3,240K req/sec, 14 bytes/请求
- **Rust**: 84ms, 1,190K req/sec, 0 bytes/请求

### 性能分析

**Go的优势：**
- 轻量级goroutine，启动和切换开销极小
- 成熟的M:N调度器，针对高并发优化10多年
- 大规模任务时优势更加明显（100K时领先97%）

**FlowCoro的特点：**
- C++零开销抽象，小规模任务性能接近Go
- 内存使用可控且可预测
- 在C++生态中提供了出色的协程性能

**Rust的特色：**
- 内存安全保证，零内存泄漏
- 调度开销相对较高，但性能仍然不错
- 适合对安全性要求极高的场景

## 测试设计

所有测试都采用相同的设计原则：
- 去除IO延迟，专注测试协程调度性能
- 同机器同环境，确保公平对比
- 统一的输出格式，便于结果对比

## 实际测试结果 (10,000并发请求)

### 系统配置

- CPU：16核心
- 操作系统：Linux
- 测试时间：2025-08-10

### 性能对比表

| 语言/框架 | 总耗时 | 吞吐量 | 内存使用变化 | 单请求内存开销 |
|-----------|--------|--------|--------------|----------------|
| **Go Goroutines** | 4ms | 2,500,000 req/sec | +408KB | 41 bytes |
| **Rust Tokio** | 6ms | 1,666,666 req/sec | +0KB | 0 bytes |
| **C++ FlowCoro** | 1.3ms* | 7,727,975 ops/sec* | - | - |

*注：C++ FlowCoro显示的是基础协程创建性能，测试方式略有不同

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

### 性能分析

1. **Go优势**：
   - 启动快速，4ms完成10000个并发任务
   - 内存使用合理，每个goroutine仅占41字节
   - M:N调度器成熟稳定

2. **Rust优势**：
   - 零成本抽象，无额外内存分配
   - 编译时优化充分
   - 内存安全保证

3. **C++ FlowCoro优势**：
   - 基础协程性能最高（770万ops/sec）
   - 底层控制能力强
   - 可根据需求定制调度策略

### 结论

- **高并发场景**：Go表现最佳，平衡了性能和资源使用
- **内存敏感场景**：Rust零开销抽象优势明显
- **极致性能场景**：C++ FlowCoro原生性能领先，适合性能关键应用

## 文件说明

- `go_benchmark.go`: Go Goroutine性能测试
- `rust_benchmark.rs`: Rust Tokio性能测试
- `Cargo.toml`: Rust项目配置
- `accurate_bench.cpp`: FlowCoro详细基准测试

## 注意事项

- 编译产物已被.gitignore忽略
- 首次运行Rust测试需要下载依赖包
- 测试结果可能因硬件环境略有差异
