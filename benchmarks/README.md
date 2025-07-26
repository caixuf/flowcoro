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
make -C ../build -j$(nproc)              # 编译FlowCoro
go build -o go_benchmark go_benchmark.go # 编译Go版本
cargo build --release                    # 编译Rust版本

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

### 预期结果 (16核Linux)
- **FlowCoro**: 2ms, 5,000,000 req/sec
- **Go**: 4ms, 2,500,000 req/sec  
- **Rust**: 6ms, 1,666,666 req/sec

## 测试设计

所有测试都采用相同的设计原则：
- 去除IO延迟，专注测试协程调度性能
- 同机器同环境，确保公平对比
- 统一的输出格式，便于结果对比

## 文件说明

- `go_benchmark.go`: Go Goroutine性能测试
- `rust_benchmark.rs`: Rust Tokio性能测试
- `Cargo.toml`: Rust项目配置
- `accurate_bench.cpp`: FlowCoro详细基准测试

## 注意事项

- 编译产物已被.gitignore忽略
- 首次运行Rust测试需要下载依赖包
- 测试结果可能因硬件环境略有差异
