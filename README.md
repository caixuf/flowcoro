# FlowCoro

A high-performance C++20 coroutine library designed for batch task processing and high-throughput scenarios.

English | [中文](README_zh.md)

## Features

- **High Performance**: Optimized for high-throughput scenarios with Profile-Guided Optimization (PGO)
- **Lock-free Architecture**: Efficient queue operations with load balancing  
- **C++20 Coroutines**: Modern coroutine-based task scheduling
- **Batch Processing**: Designed for concurrent task execution
- **Advanced Concurrency**: WhenAny, WhenAll operations with optimized scheduling
- **Channel Communication**: Thread-safe async channels for producer-consumer patterns
- **Memory Pool**: Custom memory allocation inspired by Redis/Nginx design
- **PGO Optimization**: Performance improvements through profile-guided compilation
- **Deterministic Real-Time Execution**: Single-thread-affine `RtExecutor` for robotics / control / embedded — periodic ticks, CPU pinning, zero syscall in steady state (see `flowcoro::rt`) and a ready-made [Autonomous Driving pipeline demo](examples/autonomous_driving/ad_pipeline_demo.cpp)
- **Bounded Lock-Free MPMC Channel**: `BoundedChannel<T>` (Vyukov ring — no allocation, no SMR, immediate fail when full/empty); fills the gap left by the coroutine-only `Channel<T>`
- **CPU Affinity**: `cpu_affinity.h` unifies pinning and **physical-core** enumeration (dedup by `thread_siblings_list`, so SMT siblings never go to two workers); `lockfree::ThreadPool` takes an affinity list directly
- **Python Bindings (optional)**: `-DFLOWCORO_BUILD_PYTHON=ON` builds `flowcoro_py.so` — `CoroutineThreadPool` + `when_all`/`wait_any` + `Channel`; see [Python binding docs](docs/PYTHON_BINDING.md)

## Performance

Measured numbers live in [Performance Data Reference](docs/PERFORMANCE_DATA.md). The figures below are from a **2026-09-15** Release run of `professional_flowcoro_benchmark` on the author's machine (Intel Core i7-14650HX 6C/12T, Ubuntu 24.04.4 LTS WSL2, g++ 13.3.0, FlowCoro 4.0.0, `thread_count=12`). They are not a claim of production-proven scale.

| Benchmark | Mean | Throughput |
|-----------|------|------------|
| Simple Computation | 14 ns | 69.0M ops/s |
| Coroutine Create & Execute | 132 ns | 7.60M ops/s |
| WhenAny (2 tasks) | 630 ns | 1.59M ops/s |
| LockFree Queue (enq+deq) | 132 ns | 7.56M ops/s |
| Memory Allocation (1KB) | 24 ns | 41.3M ops/s |

Some bench labels (HTTP, Echo) are CPU-side simulations, not real sockets. This run did **not** refresh Go numbers; a same-machine Rust microbench exists but is **not** the same methodology — see PERFORMANCE_DATA.md rather than “N× faster than Go/Rust” claims.

**Best suited for**: coroutine scheduling, batch / high-throughput work, plus deterministic realtime via `flowcoro::rt`.

## Quick Start

### Requirements

- C++20 compiler (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### Build

```bash
git clone https://github.com/caixuf/flowcoro.git
cd flowcoro
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Note**: PGO optimization is automatically enabled when profile data is available in `pgo_profiles/` directory.

### Basic Usage

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// Simple coroutine task
Task<int> compute(int value) {
    // Simulate work
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

// Concurrent execution
Task<void> example() {
    // Tasks start executing synchronously, then are scheduled when they suspend
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
    // Wait for results
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
    
    std::cout << "Results: " << result1 << ", " << result2 << ", " << result3 << std::endl;
    co_return;
}

// Producer-Consumer with Channel
Task<void> channel_example() {
    auto channel = make_channel<int>(10); // Buffer size: 10
    
    // Producer
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await channel->send(i);
            std::cout << "Produced: " << i << std::endl;
        }
        channel->close();
    };
    
    // Consumer
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break; // Channel closed
            std::cout << "Consumed: " << value.value() << std::endl;
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

### Python (optional)

```bash
pip install pybind11
cmake -B build-py -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_PYTHON=ON
cmake --build build-py --parallel $(nproc) --target flowcoro_py
export PYTHONPATH=$PWD/build-py/python
```

```python
import flowcoro_py as fc

with fc.CoroutineThreadPool(threads=8, pin_to_cores=True) as pool:
    futures = [pool.submit(run_case, path) for path in cases]
    results = fc.when_all(futures)      # order-preserving; raises the lowest-index failure

channel = fc.Channel(capacity=10240)    # bounded lock-free MPMC, bytes payload
channel.try_push(b"pdu")                # -> bool, non-blocking
msg = channel.try_pop()                 # -> Optional[bytes], non-blocking
```

**Read [docs/PYTHON_BINDING.md](docs/PYTHON_BINDING.md) first.** `submit` runs Python
callables on the C++ pool, so only work that releases the GIL (subprocess / IO) gets real
parallelism — measured against `concurrent.futures.ThreadPoolExecutor` it is a wash on the
shapes that matter, and slower on micro-tasks. The binding is worth it for precise core
pinning and an in-process bounded lock-free hand-off, not as a magic speedup.

## Architecture

FlowCoro uses a three-layer scheduling architecture:

```text
Task Creation → Coroutine Manager → Coroutine Pool → Thread Pool
     ↓              ↓                   ↓              ↓
suspend_never   Load Balancing    Lock-free Queue   Execution
```

- **suspend_never**: Tasks start executing synchronously on creation, then suspend at first co_await point
- **Smart Load Balancing**: Automatic scheduler selection after suspension
- **Lock-free Queues**: High-performance task distribution
- **Continuation Mechanism**: Zero-copy task chaining

### Deterministic Real-Time Execution (`flowcoro::rt`)

Alongside the high-throughput scheduler, FlowCoro ships a **single-thread deterministic real-time model** (`RtExecutor`) for latency-critical control loops (robotics / autonomous driving / embedded):

- **Single-thread affinity**: every `resume` / `destroy` happens on the executor thread; cross-thread events only `post_ready(h)` the handle back — never inline `resume`. This is what FlowEngine depends on to avoid data races.
- **Periodic tick**: `run()` is a non-blocking tick (timers → ready → resume/destroy). Lazy-start tasks (`RtTask`) run at fixed cadence; `rt::yield()` defers to the next tick, guaranteeing no re-entrancy within one tick.
- **CPU pinning**: `Config{.pin_cpu = N}` binds the executor thread to a core (`run_blocking`).
- **Zero syscall steady state**: internal re-post uses an executor-thread-local vector snapshot; cross-thread path uses an MPSC lock-free queue.
- **Two-phase shutdown**: `request_stop()` → cooperative stop checked at cycle boundary → frames `co_return` and are destroyed on the executor thread.

```cpp
#include <flowcoro/rt_executor.h>

flowcoro::rt::RtExecutor ex{{ .pin_cpu = 2 }};
ex.spawn(periodic_control_task(), "control");
ex.run_blocking();   // periodic ticks until all tasks finish
// or: while (!ex.is_finished()) ex.run();
```

See the complete usage in [API Reference §9](docs/API_REFERENCE.md#9-确定性实时执行-rtexecutor) and the [Autonomous Driving pipeline demo](examples/autonomous_driving/ad_pipeline_demo.cpp) (DDS Pub/Sub + `when_any` QoS deadline degradation).

## Use Cases

**Ideal for:**

- Web API servers (high request throughput)
- Batch data processing
- High-frequency trading systems
- Microservice gateways
- Producer-consumer pipelines
- Multi-stage data processing workflows

**With the Real-Time layer (`flowcoro::rt`):**

- Robotics / autonomous driving control loops (periodic, latency-critical)
- AMR / drone stabilization and navigation
- Embedded deterministic scheduling
- Any embedded/control workload needing single-thread affinity + CPU pinning

Try the [Autonomous Driving pipeline demo](examples/autonomous_driving/ad_pipeline_demo.cpp):

```bash
cd build && cmake .. && make ad_pipeline_demo
./examples/autonomous_driving/ad_pipeline_demo 5
```

**Enhanced with Channels:**

- Inter-coroutine communication
- Buffered message passing
- Async data pipelines
- Coordinated multi-producer/multi-consumer systems

## Benchmarks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_BENCHMARKS=ON
cmake --build build --target professional_flowcoro_benchmark -j$(nproc)
./build/benchmarks/professional_flowcoro_benchmark

# Optional scale checks (different from the professional bench)
./build/examples/hello_world 10000   # 10K tasks
./build/examples/hello_world 100000  # 100K tasks
```

## Documentation

- [Introduction (中文)](docs/INTRODUCTION.md)
- [Quick Start Guide](docs/QUICK_START.md)
- [API Reference](docs/API_REFERENCE.md)
- [Architecture Design](docs/ARCHITECTURE.md)
- [Performance Data](docs/PERFORMANCE_DATA.md)
- [PGO Optimization Guide](docs/PGO_GUIDE.md)
- [Python Bindings](docs/PYTHON_BINDING.md)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines and submit pull requests.
