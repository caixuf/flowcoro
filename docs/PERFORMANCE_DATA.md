# FlowCoro 性能数据参考

本文档是 FlowCoro 官方性能数字的来源。表格中的 FlowCoro 数值来自作者机器上一次 **Release** 跑的 `professional_flowcoro_benchmark`，日期为 **2026-09-15**。不要把下文以外的旧吞吐（例如 13.3M / 183K ops/s）当作当前结果。

## 测试环境（本次）

| 项目 | 值 |
|------|-----|
| CPU | Intel Core i7-14650HX（6 cores / 12 threads） |
| OS | Ubuntu 24.04.4 LTS（WSL2），Linux 6.6.x microsoft-standard-WSL2 |
| 编译器 | g++ 13.3.0 |
| 构建 | Release，FlowCoro 4.0.0，`thread_count=12` |
| 日期 | 2026-09-15 |
| Go | **本次未安装/未跑**，没有刷新的 Go 数字，也没有据此计算的「比 Go 快 X 倍」 |

## 如何复现

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target professional_flowcoro_benchmark -j$(nproc)
./build/benchmarks/professional_flowcoro_benchmark
```

默认 `FLOWCORO_BUILD_BENCHMARKS` 为 ON。吞吐按 `1e9 / mean_ns` 从均值延迟换算。不同 CPU、负载和 WSL2 配置会改变结果。

## 当前 FlowCoro 结果（2026-09-15）

来源：`./build/benchmarks/professional_flowcoro_benchmark`（Release）。

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

### 读这些行时要注意什么

这些名字来自基准程序的输出标签，**并不都等于真实网络或真实内存池**：

- **Simple Computation**：非协程基线（100 次整数累加）。
- **Coroutine Creation Only / Create & Execute / Void Coroutine**：`Task` 创建与完成路径。`simple_coroutine` 在 `suspend_never` 下创建时即可跑完，不是「先挂起再调度」的成本。
- **WhenAny (2 / 4)**：`when_any` 等待若干计算型 `Task`。
- **LockFree Queue (enq+deq)**：每次迭代新建 `lockfree::Queue<int>`，再 enqueue + dequeue 一次。
- **Memory Allocation (1KB)**：`std::vector<char>(1024)`。
- **Memory Pool Allocation (1KB)**：标签如此，实现目前是 `malloc(1024)` / `free`，**不是** FlowCoro 内存池 API。
- **Echo Server Throughput**：标签如此，实现是 100 次整数累加，**没有 TCP/套接字 IO**。不要当成 echo 服务器吞吐。
- **Concurrent Echo Clients**：100 个协程，每个做 1000 次乘加 + `sleep_for(1µs)`，再 `co_await` 全部完成。**不是**真实客户端连真实 echo 端口。
- **HTTP Request Processing**：对两段 HTTP 字符串做 `strlen`，**没有**解析、socket 或 HTTP 服务器。
- **Concurrent Task Processing**：三个不同 `Task` 经 `sync_wait` 跑完（数据处理 / 请求处理模拟 / 批处理）。
- **Sleep 1us**：`co_await sleep_for(1µs)`。测到的均值约 129 ns，短于 1µs，说明该路径在这个负载下并未按墙钟 sleep 满 1µs（实现/计时器分辨率问题）；不要把它读成「睡眠精度」。

## 同机 Rust（Tokio 风格 bench）— 方法不完全相同

同一次会话在同一台机器上跑了 `professional_rust_benchmark`（rustc 1.96.0）。下列吞吐是近似值，**单独列出**，不要当成与上表逐行对齐的对比：

| Rust bench（标签） | 约 Throughput |
|--------------------|----------------|
| Task create+exec | 29.5K ops/s |
| Channel | 6.04M ops/s |
| Simple compute | 72.7M ops/s |
| Concurrent tasks (10) | 867 ops/s |
| Mem alloc 1KB | 68.2M ops/s |
| Concurrent echo | 809 ops/s |
| HTTP | 71.4M ops/s |

对照源码后的差异（不完整，但足够避免误读）：

| 主题 | FlowCoro | Rust bench | 能否直接比 |
|------|----------|------------|------------|
| 简单计算 | 同步 0..100 求和 | 同步 0..100 求和 | 较接近 |
| 1KB 分配 | `vector<char>(1024)` | `vec![0u8; 1024]` | 较接近 |
| 任务创建+执行 | `simple_coroutine`（100 次累加，`suspend_never`） | `tokio::spawn` 后 await（循环 0..10） | **否** — spawn/调度模型不同 |
| HTTP | 同步 `strlen` 两个字面量 | async 里对字符串 `.len()` | 都是微基准，都不是 HTTP 服务 |
| Concurrent echo | 100 个协程 × (1000 次乘加 + 1µs sleep)，再逐个 `co_await` | 100 个 `JoinSet::spawn`，同样 1000 次乘加 + `tokio::time::sleep(1µs)` | 工作量相近，等待/调度模型不同 |
| Channel vs 无锁队列 | 新建 queue + enq/deq | `tokio::sync::mpsc` 容量 1 的 send/recv | **否** |
| Concurrent tasks | 三个异构 `Task` + `sync_wait` | 10 个只 sleep 1µs 的 tokio 任务 | **否** |

因此：**不要**用「FlowCoro 任务创建比 Rust 快 N 倍」这类倍数来描述这次数据。简单计算（69.0M vs 约 72.7M）和 1KB 分配（41.3M vs 约 68.2M）更接近同机、同形状的对照。

## Go

本次环境没有 Go 工具链，**没有刷新的 Go 数字**。

旧文档里的 Go 列和「比 Go 快 7.2× / 63×」等倍数来自未按当前 `professional_*` 程序复现的更早数字，**不再作为当前结论**。需要跨语言对比时，在同一台机器上同时跑：

- `./build/benchmarks/professional_flowcoro_benchmark`
- `go run benchmarks/professional_go_benchmark.go`（或先 `go build`）
- `cargo run --release`（目录 `benchmarks/professional_rust_benchmark`）

并说明哪些行在源码里实际测的是同一件事。

## 已隔离的历史数字（不是当前结果）

下列数字曾出现在本文件和介绍页中，**当前 `professional_flowcoro_benchmark` 没有复现它们**。仅作考古记录：

- 协程/任务创建 **13.3M ops/s**
- 并发客户端 **183K ops/s**
- 简单计算 35.1M、无锁队列 15.8M、HTTP 34.9M、内存池 35.2M 等「统一三语言表」
- `benchmarks/README.md` 里曾经的「16 核」环境与 4.20M 协程创建等「最新专业基准」表（环境与本次不符，且含未刷新的 Go 列）

`benchmarks/README.md` 里 10K 批量任务那段仍标为历史参考，方法与本表的单操作均值不同，不可互换。

---

## 真实套接字 IO 与 RT 抖动（与上表分离）

上表 Echo / HTTP / 内存池数字来自 `professional_flowcoro_benchmark` 的 **CPU-sim** 行，**不是** TCP QPS。真实测量：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target real_net_benchmark rt_cycle_jitter_benchmark -j12
./build/benchmarks/real_net_benchmark
./build/benchmarks/rt_cycle_jitter_benchmark
```

- `real_net_benchmark`：localhost 回环上的 `flowcoro::net` TCP echo（connections/s、req/s、RTT 分位数）。标明 loopback，不是网卡。持久 echo 的 fd 保持 epoll 注册（Linux `MOD`+`EPOLLONESHOT`）；短连接 wave=32 的 p99 主要是 accept 排队，不要用拉长 timeout 或改 wave 来粉饰。WSL 是性能源，不要引用 cloud QPS。
- `rt_cycle_jitter_benchmark`：`RtExecutor` 周期误差 / tardiness。墙钟测量，不是硬实时证书。

复现步骤与读数说明见 [benchmarks/README.md](../benchmarks/README.md) 文首「诚实的真实性能测量」。
