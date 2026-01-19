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

## Performance

> **Performance Data**: For detailed performance metrics and benchmarks, see [Performance Data Reference](docs/PERFORMANCE_DATA.md)

### Key Performance Highlights

- **Coroutine Creation & Execution**: Optimized performance with PGO
- **Hello World Throughput**: Competitive performance compared to traditional threading
- **WhenAny Operations**: Efficient concurrent scheduling
- **Lock-free Queue**: High-performance operations
- **Memory Pool Allocation**: Faster than standard system allocation
- **HTTP Request Processing**: Good throughput performance

### Core Performance Comparison

FlowCoro demonstrates good performance compared to Go and Rust in key areas:

- **Coroutine Creation & Execution**: Competitive performance against Go and Rust
- **Lock-free Queue**: Good performance characteristics
- **HTTP Request Processing**: Solid performance metrics
- **Simple Computation**: Efficient computational performance
- **Memory Pool Allocation**: Optimized memory management

**Best suited for**: Coroutine-based scheduling, batch processing, concurrent task management

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

## Use Cases

**Ideal for:**

- Web API servers (high request throughput)
- Batch data processing
- High-frequency trading systems
- Microservice gateways
- Producer-consumer pipelines
- Multi-stage data processing workflows

**Enhanced with Channels:**

- Inter-coroutine communication
- Buffered message passing
- Async data pipelines
- Coordinated multi-producer/multi-consumer systems

## Benchmarks

```bash
# Run performance tests
./build/benchmarks/simple_benchmark

# Test different scales
./build/examples/hello_world 10000   # 10K tasks
./build/examples/hello_world 100000  # 100K tasks
```

## Documentation

- [Quick Start Guide](docs/QUICK_START.md)
- [API Reference](docs/API_REFERENCE.md)
- [Architecture Design](docs/ARCHITECTURE.md)
- [Performance Data](docs/PERFORMANCE_DATA.md)
- [PGO Optimization Guide](docs/PGO_GUIDE.md)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines and submit pull requests.
