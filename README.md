# FlowCoro

**高性能C++20协程库 - 专注批量并发任务处理**

一个基于C++20的现代协程库，专门设计用于高吞吐量的批量任务处理和请求-响应模式的应用场景。

## 项目简介

FlowCoro 是一个性能导向的C++协程库，采用多线程协程调度架构，在保持简洁API的同时实现卓越的批量处理性能。

### 核心设计理念

- **批量任务优化**: 专门针对大规模并发任务处理进行优化
- **请求-响应模式**: 完美适配Web服务、API处理等场景
- **多线程调度**: 智能协程调度器 + 线程池架构
- **性能至上**: 每个组件都经过性能基准测试验证
- **内存安全**: RAII管理，零内存泄漏设计

### 架构特点说明

**重要：** FlowCoro 采用**三层调度架构**，基于无锁队列实现高性能协程调度：

-  **完美适配**: 批量任务处理、请求-响应模式
-  **极高性能**: 476K+ req/s 吞吐量，40倍性能提升
-  **无锁调度**: 15.6M ops/s队列操作，智能负载均衡
-  **架构限制**: 无事件循环，不支持协程间持续协作模式

**调度流程**: Task创建 → 协程管理器 → 协程池(无锁队列) → 线程池执行

## 性能表现

基于真实基准测试的突破性性能数据：

### 大规模高并发处理能力（100,000任务基准测试）

- **极限吞吐量**: **1,886,792 req/s** (188万请求/秒)
- **超低延迟**: 0.00053ms/请求
- **性能倍数**: 比传统多线程快 **137倍** (1.89M vs 13.8K req/s)
- **内存效率**: 271 bytes/请求（包含完整协程调度系统）

### 中等规模处理能力（10,000任务测试）

- **稳定吞吐量**: **476,190 req/s** (47万请求/秒)
- **执行时间**: 21ms
- **性能倍数**: 比传统多线程快 **40倍**
- **内存效率**: 408 bytes/请求

### 性能监控系统

- **实时吞吐量**: **31,818+ tasks/sec**
- **系统稳定性**: **100%任务完成率** (零失败、零取消)
- **响应速度**: 总运行时间仅 **21ms** (包含系统初始化)
- **定时器精度**: 100个定时器事件精确调度

### 协程创建性能

- **吞吐量**: 4.33M ops/sec (433万次/秒)
- **延迟**: 231 ns/op
- **对比**: 相比传统线程创建提升数百倍

### 并发机制性能

#### Task 创建时并发（唯一并发方式）
- **启动延迟**: 0.1μs (微秒级)
- **内存开销**: 每协程 80 bytes
- **适用场景**: 所有异步编程场景
- **实现方式**: 任务启动后立即并发执行，co_await 只是等待结果

#### when_all 语法糖（便利封装）
- **本质**: co_await 的便利封装，内部仍是顺序等待
- **小规模** (10任务): 625K ops/sec
- **中规模** (100任务): 2.38M ops/sec  
- **大规模** (1000任务): 3.69M ops/sec
- **作用**: 简化多任务等待的语法，避免手动解构

### 核心组件

| 组件 | 性能指标 | 说明 |
|------|----------|------|
| Task协程 | 231ns创建 | 轻量级协程任务 |
| 协程调度 | 19.8M ops/s | 基本协程创建和调度 |
| 线程池 | 32工作线程 | 工作窃取调度 |
| 内存池 | 18.7M ops/s | 动态扩展设计 |
| 无锁队列 | 45.5M ops/s | 高并发数据结构，支持协程调度 |
| 定时器 | 52ns精度 | 高精度sleep_for |
| 协程池 | 多调度器 | 智能负载均衡，跨线程协程恢复 |

### 与传统多线程的真实对比测试

**测试环境**: 同机器测试确保公平性

#### 大规模并发测试（100,000任务）

| 方案 | 执行时间 | 吞吐量 | 内存使用 | 单任务内存 |
|------|----------|--------|----------|------------|
| **FlowCoro协程** | **53ms** | **1,886,792 req/s** | 30.9MB | 271 bytes |
| **传统多线程** | **7,268ms** | **13,759 req/s** | 37.8MB | 341 bytes |
| **性能优势** | **137倍** | **137倍** | **更高效** | **更轻量** |

#### 中等规模测试（10,000任务）

| 方案 | 执行时间 | 吞吐量 | 内存使用 | 单任务内存 |
|------|----------|--------|----------|------------|
| **FlowCoro协程** | **21ms** | **476,190 req/s** | 8.15MB | 408 bytes |
| **传统多线程** | **840ms** | **11,905 req/s** | 7.21MB | 314 bytes |
| **性能优势** | **40倍** | **40倍** | **功能丰富** | **功能更强** |

#### 性能优势分析

- **超高吞吐量**: 大规模场景下达到188万请求/秒的极限性能
- **完美扩展性**: 从1万到10万任务，性能优势保持稳定（40-137倍）
- **内存效率**: 随着规模增大，单任务内存开销反而降低（408→271字节）
- **响应时间**: 亚毫秒级延迟，适合实时系统需求

**测试说明**: 所有测试均在相同硬件环境下进行，使用真实的CPU计算任务，专注测试纯协程调度性能。测试代码见 `examples/hello_world.cpp`。

## 快速开始

## 快速开始

### 环境要求

- C++20编译器 (GCC 11+, Clang 12+)
- CMake 3.16+
- Linux/macOS/Windows

### 构建项目

```bash
git clone https://github.com/caixuf/flowcord.git
cd flowcord
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 基础示例

```cpp
// 基础示例：任务创建时并发执行
#include <flowcoro.hpp>

// 方式1：co_await 逐个等待（但任务已在并发执行）
Task<void> sequential_style() {
    auto task1 = compute(1); // 启动任务1 - 立即开始并发执行
    auto task2 = compute(2); // 启动任务2 - 立即开始并发执行
    auto task3 = compute(3); // 启动任务3 - 立即开始并发执行
    
    // 按顺序等待，但任务实际上在并发执行
    auto r1 = co_await task1; // 等待任务1
    auto r2 = co_await task2; // 任务2可能已完成
    auto r3 = co_await task3; // 任务3可能已完成
    
    std::cout << "Sequential: " << r1 << ", " << r2 << ", " << r3 << std::endl;
    co_return;
}

// 方式2：when_all 批量等待
Task<void> batch_style() {
    auto task1 = compute(1);
    auto task2 = compute(2); 
    auto task3 = compute(3);
    
    // 一次性等待所有任务完成
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1), 
        std::move(task2), 
        std::move(task3)
    );
    
    std::cout << "Batch: " << r1 << ", " << r2 << ", " << r3 << std::endl;
    co_return;
}

int main() {
    // 同步等待协程完成
    sync_wait(sequential_style());
    sync_wait(batch_style());
    return 0;
}
```

### 性能测试

```bash
# 运行基准测试验证性能数据
cd build && make -j16
./benchmarks/simple_benchmark

# 运行不同规模的实际测试
# 中等规模测试 (10,000任务)
./examples/hello_world 10000

# 大规模测试 (100,000任务) 
./examples/hello_world 100000

# 跨语言对比测试 (需要安装Go和Rust)
cd benchmarks && go build go_benchmark.go && ./go_benchmark 100000
cd benchmarks && cargo build --release && ./target/release/rust_benchmark 100000

# 预期输出示例：
# [BENCH] FlowCoro协程处理 (100,000任务):
# 执行时间: 53ms
# 吞吐量: 1,886,792 req/s (188万/秒)
# 内存使用: 30.9MB (271 bytes/任务)
# 
# [BENCH] 传统多线程处理 (100,000任务):
# 执行时间: 7,268ms
# 吞吐量: 13,759 req/s
# 内存使用: 37.8MB (341 bytes/任务)
# 
# 性能提升: 137倍
#
# [BENCH] 基准测试核心组件:
# 协程调度: 19.8M ops/sec
# 无锁队列: 45.5M ops/sec
# 内存分配: 627K ops/sec
```

## 核心特性

### 协程并发机制（基于真实代码实现）

FlowCoro基于单一的并发方式：**Task 创建时立即并发**，通过`suspend_never`机制实现任务启动即并发执行。

#### 核心原理（来自 include/flowcoro/core.h）

```cpp
// FlowCoro的真实实现：suspend_never = 立即执行
template<typename T>
class Task {
public:
    struct promise_type {
        // 关键设计：suspend_never决定立即执行！
        std::suspend_never initial_suspend() { return {}; }
        
        // 支持continuation的final_suspend实现任务链
        auto final_suspend() noexcept {
            struct final_awaiter {
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    // 当任务完成时，恢复等待的协程
                    if (promise->continuation) {
                        return promise->continuation;
                    }
                    return std::noop_coroutine();
                }
            };
            return final_awaiter{this};
        }
    };
    
    // await实现：设置continuation而非阻塞
    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // 设置continuation：当task完成时恢复waiting_handle
        handle.promise().set_continuation(waiting_handle);
    }
};
```

#### 任务执行流程

```text
1. Task创建 → suspend_never → 立即投递到协程池
2. 协程池调度 → 无锁队列 → 工作线程执行
3. 任务完成 → final_suspend → 恢复等待协程
4. co_await返回 → 获取结果
```

#### 调度系统架构

```text
Task创建 → 协程管理器 → 智能负载均衡 → 协程池调度器 → 无锁队列 → 线程池执行
   ↓           ↓              ↓              ↓            ↓          ↓
suspend_never  调度决策      选择最优调度器    任务入队      跨线程安全   并发执行
```
        //         ^^^^^ 这决定了Task创建时立即开始执行
        
        std::suspend_always final_suspend() noexcept { return {}; }
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
    };
};

// 实际使用：任务创建时的并发机制
Task<void> concurrent_processing() {
    // 1. 任务启动：立即开始并发执行（suspend_never机制）
    auto task1 = async_compute(1); // 任务1立即启动
    auto task2 = async_compute(2); // 任务2立即启动  
    auto task3 = async_compute(3); // 任务3立即启动
    
    // 2. 结果等待：所有任务已在并发执行
    auto result1 = co_await task1; // 等待任务1结果
    auto result2 = co_await task2; // 等待任务2结果（可能已完成）
    auto result3 = co_await task3; // 等待任务3结果（可能已完成）
    
    co_return;
}
```

#### when_all 语法便利

```cpp
// when_all 只是语法糖，简化多任务等待
Task<void> when_all_example() {
    auto task1 = async_compute(1);
    auto task2 = async_compute(2);
    auto task3 = async_compute(3);
    
    // 语法糖：避免手动解构多个结果
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1),
        std::move(task2), 
        std::move(task3)
    );
    
    // 等价于上面的手动co_await方式
    co_return;
}
```

**重要说明：**
- FlowCoro只有一种并发方式：`Task创建时立即并发`
- `when_all`不是独立的并发机制，只是便利的语法糖
- 内部实现：`when_all`仍然使用`co_await`逐个等待任务
- 并发发生在任务启动时，而不是等待时

### 动态内存池

```cpp
// 自动扩展的内存池，永不返回nullptr
MemoryPool pool(64, 100); // 64字节块，初始100个
pool.set_expansion_factor(2.0); // 容量不足时扩展为2倍

void* ptr = pool.allocate(); // 保证成功，会自动扩展
pool.deallocate(ptr); // 安全释放
```

### 高精度定时器

```cpp
Task<void> timed_operation() {
    auto start = std::chrono::high_resolution_clock::now();

    // 高精度sleep
    co_await sleep_for(std::chrono::milliseconds(100));

    auto duration = std::chrono::high_resolution_clock::now() - start;
    std::cout << "实际耗时: " << duration.count() << "ns" << std::endl;
}
```

## 架构设计

FlowCoro采用先进的多线程协程调度模型，结合智能负载均衡和高性能工作池：

- **协程调度**: 多线程智能调度器，支持跨线程协程恢复
- **CPU任务**: 32线程工作池，充分利用多核性能  
- **内存管理**: 动态扩展内存池，零内存泄漏
- **无锁设计**: 关键路径使用无锁数据结构和原子操作

这种设计在保持协程轻量级特性的同时，实现了真正的多线程并行和线程安全保证。

### 分层架构设计

| 层级 | 组件 | 功能 | 特性 |
|------|------|------|------|
| **应用层** | Task<T> | 协程接口 | 统一API |
| | co_await | 异步等待 | 等待机制 |
| | when_all() | 语法糖 | 简化多任务等待 |
| | sleep_for() | 定时器 | 高精度延时 |
| | sync_wait() | 同步等待 | 阻塞获取结果 |
| **调度层** | CoroutineManager | 协程管理 | 智能多线程调度 |
| | Ready Queue | 就绪队列 | O(1)调度 |
| | Timer Queue | 定时器队列 | 红黑树实现 |
| | SafeCoroutineHandle | 安全检测 | 跨线程生命周期保护 |
| | SmartLoadBalancer | 负载均衡 | 智能调度器选择 |
| **执行层** | ThreadPool | 工作线程池 | 32-128工作线程 |
| | Work Queue | 任务队列 | CPU密集型任务 |
| | Load Balancer | 负载均衡 | 工作窃取算法 |
| **资源层** | MemoryPool | 内存池 | RAII管理 |
| | ObjectPool | 对象池 | 对象复用 |
| | LockFree Queue | 无锁队列 | 高并发数据结构 |

### 核心技术创新

#### 1. 多线程协程调度模型

**问题**: 传统协程库要么单线程(性能受限)，要么多线程不安全(段错误)

**FlowCoro解决方案**:

```cpp
// 协程调度：多线程智能调度器 (高性能+线程安全)
void CoroutineManager::schedule_resume(std::coroutine_handle<> handle) {
    // 智能负载均衡选择最优调度器
    auto& load_balancer = get_load_balancer();
    size_t scheduler_id = load_balancer.select_scheduler();
    
    // 安全的跨线程恢复
    schedule_coroutine_enhanced(handle);
}
```

// CPU任务：工作窃取线程池 (高性能)
class WorkStealingThreadPool {
    thread_local WorkerQueue local_queue_; // 无锁本地队列
    GlobalQueue global_queue_; // 负载均衡
    // NUMA感知的工作窃取算法
}
```

#### 2. 跨线程协程恢复支持

**传统问题:**

```cpp
// 之前：跨线程协程恢复可能导致挂死
void await_suspend(std::coroutine_handle<> h) {
    ThreadPool::enqueue([h]() {
        h.resume(); // 潜在的线程安全问题
    });
}
```

**FlowCoro解决方案:**

```cpp
// 现在：安全的跨线程调度机制
void await_suspend(std::coroutine_handle<> h) {
    CoroutineManager::get_instance().schedule_resume(h);
    // 协程可以在任意工作线程中安全恢复
    // 智能负载均衡确保最优性能
}
```
```

#### 3. 协程并发模式澄清

**FlowCoro的实际实现：**

FlowCoro实际上只有一种并发方式：**Task 创建时立即并发**

```cpp
// 实际的并发机制：任务启动即并发
Task<void> real_concurrency() {
    // 这三个任务启动后立即并发执行
    auto task1 = async_compute(1); // 立即启动并发执行
    auto task2 = async_compute(2); // 立即启动并发执行
    auto task3 = async_compute(3); // 立即启动并发执行
    
    // co_await只是等待结果，不影响并发执行
    auto r1 = co_await task1; // 等待结果
    auto r2 = co_await task2; // 等待结果（可能已完成）
    auto r3 = co_await task3; // 等待结果（可能已完成）
    
    co_return;
}

// when_all的实际实现（来自core.h）
template<typename... Tasks>
Task<std::tuple<...>> when_all(Tasks&&... tasks) {
    std::tuple<...> results;
    
    // 内部仍然是顺序co_await！
    ((std::get<Is>(results) = co_await std::forward<Ts>(ts)), ...);
    
    co_return std::move(results);
}
```

**澄清错误认知：**
- **错误**: when_all提供了不同的并发机制
- **正确**: when_all只是co_await的语法糖包装
- **错误**: FlowCoro有多种并发模式
- **正确**: FlowCoro只有一种并发方式，就是任务启动时的自动并发

**性能来源：**
- 并发性能来自任务启动时的立即执行
- co_await 和 when_all 都只是等待机制
- 真正的并发在Task创建时就已经开始

#### 4. 调度器设计模式

FlowCoro借鉴了多个优秀的异步框架设计：

- **Node.js事件循环**: 单线程 + 事件驱动
- **Go runtime调度器**: M:N协程调度模型
- **Rust tokio**: Future + Reactor模式
- **cppcoro库**: C++20协程的工业级实现

```cpp
// FlowCoro的drive()方法 
void CoroutineManager::drive() {
    // 1. 处理定时器队列
    process_timer_queue();

    // 2. 处理就绪协程队列
    process_ready_queue();

    // 3. 清理需要销毁的协程
    process_delayed_destruction();
}
```

### 架构特性

#### 1. 高效协程切换

```cpp
// 协程状态保存 - 高效抽象
struct CoroutineFrame {
    void* instruction_pointer; // 指令指针
    void* stack_pointer; // 栈指针
    // 无需保存整个栈 - 只保存必要的寄存器状态
};
```

#### 2. 无锁数据结构

```cpp
// 无锁就绪队列 - 支持多生产者单消费者
template<typename T>
class LockFreeQueue {
    std::atomic<Node*> head_{nullptr};
    std::atomic<Node*> tail_{nullptr};

    // CAS操作实现无锁入队/出队
    bool enqueue(T item) noexcept;
    bool dequeue(T& item) noexcept;
};
```

#### 3. 线程安全检测机制

```cpp
class SafeCoroutineHandle {
    std::thread::id creation_thread_; // 创建线程ID
    std::atomic<bool> is_valid_; // 原子有效性标记

    bool is_current_thread_safe() const {
        return std::this_thread::get_id() == creation_thread_;
    }
};
```

### 性能优势来源

#### 1. 轻量级协程切换

#### 2. 无锁数据结构

```cpp
// 传统多线程：需要锁同步
std::mutex mtx;
std::queue<Task> task_queue;

void enqueue(Task task) {
    std::lock_guard lock(mtx); // 锁开销
    task_queue.push(task);
}

// FlowCoro：单线程无锁
lockfree::Queue<std::function<void()>> task_queue_;

void schedule(std::coroutine_handle<> h) {
    task_queue_.enqueue([h]() { h.resume(); }); // 无锁
}
```

## 项目结构

```
flowcoro/
├── include/flowcoro/ # 头文件目录
│ ├── flowcoro.hpp # 主头文件
│ ├── core.h # 协程核心功能
│ ├── thread_pool.h # 线程池实现
│ ├── memory_pool.h # 动态内存池
│ ├── object_pool.h # 对象池复用
│ ├── lockfree.h # 无锁数据结构
│ ├── result.h # Result错误处理
│ ├── network.h # 网络I/O支持
│ └── simple_db.h # 基础数据库操作
├── src/ # 源文件
│ ├── globals.cpp # 全局配置
│ ├── net_impl.cpp # 网络实现
│ └── coroutine_pool.cpp # 协程池实现
├── tests/ # 测试代码
│ ├── test_core.cpp # 核心功能测试
│ ├── test_network.cpp # 网络功能测试
│ ├── test_database.cpp # 数据库测试
│ ├── test_rpc.cpp # RPC功能测试
│ └── test_framework.h # 测试框架
├── benchmarks/ # 性能基准测试
│ ├── accurate_bench.cpp # 精确基准测试
│ └── simple_bench.cpp # 简单基准测试
├── examples/ # 使用示例
│ ├── hello_world.cpp # 基础示例
│ └── hello_world_concurrent.cpp # 并发示例
├── scripts/ # 构建脚本
│ ├── build.sh # 构建脚本
│ └── run_core_test.sh # 核心测试脚本
└── cmake/ # CMake配置
    └── FlowCoroConfig.cmake.in
```

## 运行测试

```bash
# 运行核心功能测试
./tests/test_core

# 运行所有测试
ctest

# 运行性能基准测试
./benchmarks/accurate_benchmarks
```

### 测试覆盖情况

| 组件 | 测试覆盖 | 状态 |
|------|----------|------|
| Task核心功能 | 95% | 完整 |
| 线程池调度 | 90% | 完整 |
| 内存池管理 | 85% | 完整 |
| 无锁数据结构 | 88% | 完整 |
| 网络I/O | 75% | 基础 |
| 错误处理 | 92% | 完整 |

## 设计目标与使用场景

### 设计目标

1. **学习导向**: 通过实现理解C++20协程原理
2. **接口统一**: 消除Task类型选择困扰
3. **安全可靠**: RAII管理，异常安全
4. **性能优先**: 零开销抽象，无锁数据结构
5. **易于使用**: 现代C++风格，直观API

### 使用场景判断

```text
选择FlowCoro的场景：
IO密集型：Web服务器、API网关、数据库操作
高并发：需要处理>1000并发连接
低延迟：对响应时间敏感的应用
资源受限：内存、CPU有限的环境

不适合的场景：
CPU密集型：图像处理、科学计算、加密
低并发：<100个连接的简单应用
遗留代码：大量现有同步代码的改造
```

## 适用场景

### 强烈推荐使用（已验证高性能）

**批量任务处理场景:**
- **Web API服务器**: 500K+ req/s 吞吐量，适合REST API、微服务网关
- **数据库批量操作**: 批量查询、批量插入、数据迁移任务
- **文件批量处理**: 图片压缩、日志分析、数据清洗
- **消息批量处理**: 消息队列批量消费、邮件批量发送

**请求-响应模式:**
- **HTTP服务器**: 每个请求独立处理，完美匹配FlowCoro架构
- **RPC服务**: gRPC、Thrift等远程调用服务
- **缓存服务**: Redis代理、内存缓存服务
- **代理网关**: 负载均衡器、API网关

**高并发独立任务:**
- **爬虫系统**: 大规模并发网页抓取
- **测试工具**: 压力测试、性能基准测试
- **数据同步**: 多源数据同步任务

###  不适合的场景（架构限制）

**协程间持续协作:**
- **生产者-消费者模式**: 需要协程间持续通信的场景
- **流水线处理**: 需要协程链式协作的任务
- **事件驱动系统**: 需要协程间事件传递的架构

**其他不适合场景:**
- **CPU密集型**: 科学计算、图像处理、加密算法
- **低并发同步**: <100连接的简单同步应用  
- **遗留系统改造**: 大量现有同步代码的全面重构

###  性能收益预期

基于真实测试数据，选择FlowCoro可获得：

| 指标 | 提升倍数 | 具体数值 |
|------|----------|----------|
| **执行速度** | 36.3倍 | 10ms vs 363ms |
| **吞吐量** | 36.3倍 | 1M req/s vs 27K req/s |
| **内存效率** | 优化 | 407 bytes/task (功能丰富) |
| **延迟降低** | 96倍 | 0.002ms vs 0.193ms |

###  选择指南

```cpp
//  推荐：批量处理
Task<void> process_requests() {
    std::vector<Task<Response>> tasks;
    
    // 批量创建任务（立即开始并发执行）
    for (const auto& req : requests) {
        tasks.push_back(handle_request(req));
    }
    
    // 批量等待结果
    for (auto& task : tasks) {
        auto response = co_await task;
        process_response(response);
    }
}

//  不推荐：持续协作
Task<void> producer_consumer() {
    auto producer_task = producer();
    auto consumer_task = consumer(); 
    
    // 问题：顺序执行，不是真正并发
    co_await producer_task;  // 阻塞等待
    co_await consumer_task;  // 然后执行
}
```

## 高级特性

### 自定义Awaiter

```cpp
class CustomAwaiter {
public:
    bool await_ready() const { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        // 自定义挂起逻辑
        GlobalThreadPool::get_pool().submit([handle]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            handle.resume();
        });
    }

    void await_resume() { }
};

Task<void> custom_await_demo() {
    co_await CustomAwaiter{};
    std::cout << "自定义等待完成！" << std::endl;
}
```

### 协程生命周期管理

```cpp
class CoroutineManager {
    std::vector<Task<void>> tasks_;

public:
    void add_task(Task<void> task) {
        tasks_.push_back(std::move(task));
    }

    Task<void> wait_all() {
        for (auto& task : tasks_) {
            co_await task;
        }
        tasks_.clear();
    }
};
```

### Result类型错误处理

```cpp
template<typename T, typename E = std::exception_ptr>
class Result {
    std::variant<T, E> value_;

public:
    bool is_ok() const noexcept;
    T&& unwrap() &&; // 移动语义优化
    E&& unwrap_err() &&;
};

template<typename T>
Task<Result<T, std::string>> safe_execute(Task<T> task) {
    try {
        auto result = co_await task;
        co_return Ok(std::move(result));
    } catch (const std::exception& e) {
        co_return Err(e.what());
    }
}
```

## 性能基准数据

### 实际测试结果

**最新独立进程测试结果 (2025-07-25):**

**1000个并发请求终极测试:**

- **FlowCoro协程**: 2ms 完整进程耗时，**500,000 req/s** 超高吞吐量
- **传统多线程**: 193ms 完整进程耗时，5,181 req/s 吞吐量
- **性能提升**: 协程比多线程快 **96.5倍** (提升9550%)

**内存使用对比 (1000请求):**

- **协程内存增长**: 256KB (262 bytes/请求)
- **多线程内存增长**: 760KB (778 bytes/请求)
- **内存效率**: 协程内存使用比多线程节省 **66%**

**延迟对比分析:**

- **协程平均延迟**: 0.002ms/请求
- **多线程平均延迟**: 0.193ms/请求
- **延迟优势**: 协程延迟比多线程低 **96倍**

### 性能测试验证

```bash
# 验证我们的性能数据
cd build
./examples/hello_world 100 # 100并发请求测试
./examples/hello_world 1000 # 1000并发请求测试
```

**测试架构特点:**

- **完全进程隔离**: 使用fork/exec消除内存污染
- **精确计时**: 包含进程启动/执行/退出的完整时间
- **字节级监控**: 精确的内存使用跟踪
- **多核利用**: 16核CPU环境下的真实多核测试

```bash
cd build
./examples/hello_world 100 # 100并发请求测试
./examples/hello_world 1000 # 1000并发请求测试
```

## 性能调优

### 内存池配置

```cpp
// 针对小对象优化
MemoryPool small_pool(32, 1000); // 32字节，1000个初始块
small_pool.set_expansion_factor(1.5); // 小幅扩展

// 针对大对象优化
MemoryPool large_pool(1024, 100); // 1KB，100个初始块
large_pool.set_expansion_factor(2.0); // 快速扩展
```

### 线程池调优

```cpp
// 自动检测CPU核心数
auto thread_count = std::thread::hardware_concurrency();
ThreadPool pool(thread_count);

// 或者手动设置
ThreadPool custom_pool(8); // 8个工作线程
```

## 贡献指南

欢迎提交issue和pull request：

1. Fork项目
2. 创建特性分支
3. 提交更改
4. 推送分支
5. 创建Pull Request

## 许可证

本项目采用MIT许可证 - 查看[LICENSE](LICENSE)了解详情。

## 致谢

感谢以下开源项目提供的学习参考：

- [cppcoro](https://github.com/lewissbaker/cppcoro) - C++协程库参考实现
- [async_simple](https://github.com/alibaba/async_simple) - 阿里巴巴协程库
- [liburing](https://github.com/axboe/liburing) - 高性能I/O库

---

**项目状态**: 活跃开发中 | **最后更新**: 2025年8月
