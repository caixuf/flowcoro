# FlowCoro

A high-performance C++20 coroutine library designed for batch task processing and high-throughput scenarios.

English | [中文](README_zh.md)

## Features

- **High Performance**: 1.9M req/s throughput, 137x faster than traditional threading
- **Lock-free Architecture**: 45.5M ops/s queue operations with smart load balancing
- **C++20 Coroutines**: Modern coroutine-based task scheduling
- **Batch Processing**: Optimized for large-scale concurrent task execution
- **Memory Efficient**: 271 bytes per task with full scheduling system

## Performance

| Test Scale | Throughput | Latency | vs Threads |
|------------|------------|---------|------------|
| 100K tasks | 1.89M req/s | 0.53μs | 137x faster |
| 10K tasks | 476K req/s | 2.1ms | 40x faster |

## Quick Start

### Requirements

- C++20 compiler (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### Build

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

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
    // Tasks start executing immediately upon creation
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

int main() {
    sync_wait(example());
    return 0;
}
```

## Architecture

FlowCoro uses a three-layer scheduling architecture:

```text
Task Creation → Coroutine Manager → Coroutine Pool → Thread Pool
     ↓              ↓                   ↓              ↓
suspend_never   Load Balancing    Lock-free Queue   Execution
```

- **suspend_never**: Tasks execute immediately upon creation
- **Smart Load Balancing**: Automatic scheduler selection
- **Lock-free Queues**: High-performance task distribution
- **Continuation Mechanism**: Zero-copy task chaining

## Use Cases

**Ideal for:**

- Web API servers (500K+ req/s)
- Batch data processing
- High-frequency trading systems
- Microservice gateways

**Not suitable for:**

- Producer-consumer patterns requiring inter-coroutine communication
- Event-driven real-time systems

## Benchmarks

```bash
# Run performance tests
./build/benchmarks/simple_benchmark

# Test different scales
./build/examples/hello_world 10000   # 10K tasks
./build/examples/hello_world 100000  # 100K tasks
```

## Documentation

- [API Reference](docs/API_REFERENCE.md)
- [Coroutine Scheduler](docs/COROUTINE_SCHEDULER.md)
- [Usage Guide](docs/USAGE.md)
- [Task Execution Flow](docs/TASK_EXECUTION_FLOW.md)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines and submit pull requests.
