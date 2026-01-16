# FlowCoro

C++20

[English](README.md) | 

## 

- ****: PGO
- ****: 
- **C++20**: 
- ****: 
- ****: WhenAnyWhenAll
- **Channel**: -
- ****: Redis/Nginx
- **PGO**: Profile-Guided

## 

> ****:  [](docs/PERFORMANCE_DATA.md)

### 

- ****: PGO
- **Hello World**: 
- **WhenAny**: 
- ****: 
- ****: 
- **HTTP**: 

### 

FlowCoroGoRust

- ****: GoRust
- ****: 
- **HTTP**: 
- ****: 
- ****: 

****: 

## 

### 

- C++20 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### 

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

****: `pgo_profiles/`PGO

### 

```cpp
#include <flowcoro.hpp>
using namespace flowcoro;

// 
Task<int> compute(int value) {
    // 
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return value * 2;
}

// 
Task<void> example() {
    // 
    auto task1 = compute(10);
    auto task2 = compute(20);
    auto task3 = compute(30);
    
    // 
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
    
    std::cout << ": " << result1 << ", " << result2 << ", " << result3 << std::endl;
    co_return;
}

// Channel-
Task<void> channel_example() {
    auto channel = make_channel<int>(10); // : 10
    
    // 
    auto producer = [channel]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            co_await channel->send(i);
            std::cout << ": " << i << std::endl;
        }
        channel->close();
    };
    
    // 
    auto consumer = [channel]() -> Task<void> {
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break; // Channel
            std::cout << ": " << value.value() << std::endl;
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

## 

FlowCoro

```text
 →  →  → 
   ↓         ↓        ↓      ↓
suspend_never      
```

- **suspend_never**: 
- ****: 
- ****: 
- ****: 

## 

**:**

- Web API ()
- 
- 
- 
- -
- 

**Channel:**

- 
- 
- 
- /

## 

```bash
# 
./build/benchmarks/simple_benchmark

# 
./build/examples/hello_world 10000   # 1
./build/examples/hello_world 100000  # 10
```

## 

- [](docs/QUICK_START.md)
- [API](docs/API_REFERENCE.md)
- [](docs/ARCHITECTURE.md)
- [](docs/PERFORMANCE_DATA.md)
- [PGO](docs/PGO_GUIDE.md)

## 

MIT -  [LICENSE](LICENSE) 

## 

Pull Request
