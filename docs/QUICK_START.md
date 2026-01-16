# FlowCoro 

C++20

## 

- C++20 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

## 

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 
Task<int> compute(int value) {
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

// 
Task<void> example() {
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
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

## Channel

```cpp
Task<void> channel_example() {
    auto channel = make_channel<int>(10);
    
    // 
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await channel->send(i);
        }
        channel->close();
    };
    
    // 
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            std::cout << "Received: " << value.value() << std::endl;
        }
    };
    
    auto prod_task = producer();
    auto cons_task = consumer();
    
    co_await prod_task;
    co_await cons_task;
    co_return;
}
```

## 

```bash
# 
./build/tests/test_core

# 
./build/benchmarks/professional_flowcoro_benchmark

# 
./build/examples/hello_world 10000
```

## 

- [API](API_REFERENCE.md) - API
- [](ARCHITECTURE.md) - 
- [](PERFORMANCE_DATA.md) - 
- [PGO](PGO_GUIDE.md) - Profile-Guided Optimization

## 

- ****: PGO/
- ****: 
- **C++20**: 
- **Channel**: 
- ****: 
