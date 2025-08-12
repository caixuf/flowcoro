# FlowCoro

A high-performance C++20 coroutine library designed for batch task processing and high-throughput scenarios.

English | [中文](README_zh.md)

## Features

- **High Performance**: 4.20M ops/s coroutine creation and execution
- **Lock-free Architecture**: 9.61M ops/s queue operations with smart load balancing
- **C++20 Coroutines**: Modern coroutine-based task scheduling
- **Batch Processing**: Optimized for large-scale concurrent task execution
- **Advanced Concurrency**: WhenAny, WhenAll with specialized scheduling performance

## Performance

### Core Performance Comparison

| Metric | FlowCoro | Go | Rust | Notes |
|--------|----------|-----|------|-------|
| **Coroutine Creation & Execution** | 4.20M ops/s | 2.27M ops/s | 19.5K ops/s | 1.85x faster than Go |
| **Lock-free Queue** | 9.61M ops/s | 11.59M ops/s | 9.15M ops/s | Competitive performance |
| **HTTP Request Processing** | 36.77M ops/s | 41.99M ops/s | 45.42M ops/s | Industry-level performance |
| **Simple Computation** | 46.19M ops/s | 21.82M ops/s | 46.34M ops/s | Comparable to Rust |
| **Memory Allocation (1KB)** | 10.18M ops/s | 41.43M ops/s | 46.75M ops/s | Room for improvement |

**Specialized for**: Complex coroutine scheduling, batch processing, concurrent task management

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
