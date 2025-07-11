# FlowCoro 性能调优指南

## 概述

FlowCoro 设计为高性能协程库，但要充分发挥其性能优势，需要遵循一些最佳实践和调优技巧。本指南将帮助你优化应用程序的性能。

## 协程性能优化

### 1. 协程创建优化

**避免频繁创建短生命周期协程**

```cpp
// ❌ 不好的做法 - 每次都创建新协程
Task<int> bad_compute(int x) {
    co_return x * 2;  // 简单计算不值得使用协程
}

// ✅ 好的做法 - 只对真正的异步操作使用协程
Task<std::string> good_fetch_data(const std::string& url) {
    auto response = co_await http_client.get(url);  // 真正的异步IO
    co_return response.body();
}
```

**协程复用**

```cpp
// ✅ 使用协程池复用协程对象
class CoroutinePool {
private:
    ObjectPool<Task<void>> task_pool_;
    
public:
    template<typename F>
    Task<void> submit(F&& func) {
        auto task = task_pool_.acquire();
        // 重用协程对象执行新任务
        co_return co_await execute_on_reused_coro(std::forward<F>(func));
    }
};
```

### 2. 协程栈优化

**减少协程栈使用**

```cpp
// ❌ 避免在协程中分配大型局部变量
Task<void> bad_large_stack() {
    char large_buffer[1024 * 1024];  // 1MB栈空间
    // ... 使用缓冲区
    co_return;
}

// ✅ 使用堆分配或预分配缓冲区
Task<void> good_heap_allocation() {
    auto buffer = std::make_unique<char[]>(1024 * 1024);
    // ... 使用缓冲区
    co_return;
}
```

### 3. co_await 优化

**批量等待优化**

```cpp
// ❌ 串行等待
Task<void> serial_await() {
    auto result1 = co_await async_operation1();
    auto result2 = co_await async_operation2();
    auto result3 = co_await async_operation3();
    // 总时间 = time1 + time2 + time3
}

// ✅ 并行等待
Task<void> parallel_await() {
    auto task1 = async_operation1();
    auto task2 = async_operation2(); 
    auto task3 = async_operation3();
    
    auto result1 = co_await task1;
    auto result2 = co_await task2;
    auto result3 = co_await task3;
    // 总时间 = max(time1, time2, time3)
}
```

## 无锁数据结构优化

### 1. 队列性能调优

**选择合适的队列大小**

```cpp
// 根据吞吐量估算队列大小
size_t estimate_queue_size(size_t producer_count, size_t consumer_count, 
                          size_t avg_produce_rate, size_t avg_consume_rate) {
    // 考虑生产者和消费者的不平衡
    double imbalance_factor = static_cast<double>(avg_produce_rate) / avg_consume_rate;
    size_t base_size = producer_count * 64;  // 每个生产者64个槽位
    
    if (imbalance_factor > 1.2) {
        return base_size * static_cast<size_t>(imbalance_factor * 2);
    }
    return base_size;
}

// 使用估算的大小创建队列
auto queue_size = estimate_queue_size(4, 2, 10000, 8000);
LockfreeQueue<Task*> task_queue(queue_size);
```

**内存对齐优化**

```cpp
// ✅ 确保队列元素缓存行对齐
struct alignas(64) CacheAlignedTask {
    Task<void> task;
    std::atomic<bool> completed{false};
    char padding[64 - sizeof(Task<void>) - sizeof(std::atomic<bool>)];
};

LockfreeQueue<CacheAlignedTask> aligned_queue(1024);
```

### 2. 栈性能优化

**避免 ABA 问题的开销**

```cpp
// 使用版本号避免ABA问题的性能开销
template<typename T>
class OptimizedLockfreeStack {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        std::atomic<uint64_t> version{0};
    };
    
    std::atomic<Node*> head_{nullptr};
    
public:
    void push(const T& item) {
        auto new_node = std::make_unique<Node>();
        new_node->data = item;
        
        Node* current_head = head_.load();
        do {
            new_node->next = current_head;
        } while (!head_.compare_exchange_weak(current_head, new_node.release()));
    }
};
```

## 线程池优化

### 1. 线程数量调优

**基于硬件和工作负载调整线程数**

```cpp
size_t optimal_thread_count() {
    size_t hardware_threads = std::thread::hardware_concurrency();
    
    // CPU密集型任务
    if (is_cpu_intensive_workload()) {
        return hardware_threads;
    }
    
    // IO密集型任务  
    if (is_io_intensive_workload()) {
        return hardware_threads * 2;  // 可以超订阅
    }
    
    // 混合型任务
    return hardware_threads + hardware_threads / 2;
}

// 动态调整线程池大小
GlobalThreadPool::get().resize(optimal_thread_count());
```

### 2. 任务分配优化

**减少跨线程通信**

```cpp
// ✅ 使用线程局部存储减少竞争
thread_local LockfreeQueue<Task<void>*> local_task_queue(256);

void submit_local_task(Task<void>* task) {
    // 优先提交到本线程的局部队列
    if (!local_task_queue.enqueue(task)) {
        // 局部队列满了，提交到全局队列
        GlobalThreadPool::get().submit_task(task);
    }
}
```

**工作窃取优化**

```cpp
class OptimizedWorkerThread {
private:
    LockfreeQueue<Task<void>*> private_queue_;  // 私有队列
    std::vector<WorkerThread*> neighbors_;      // 邻居线程
    
public:
    bool try_steal_task() {
        // 按距离优先级窃取任务（NUMA感知）
        for (auto* neighbor : neighbors_) {
            Task<void>* task;
            if (neighbor->try_pop_task(task)) {
                return true;
            }
        }
        return false;
    }
};
```

## 网络IO优化

### 1. 缓冲区管理

**使用缓冲区池**

```cpp
class BufferPool {
private:
    ObjectPool<std::vector<uint8_t>> buffer_pool_;
    static constexpr size_t BUFFER_SIZE = 64 * 1024;  // 64KB
    
public:
    BufferPool() : buffer_pool_(100) {  // 预分配100个缓冲区
        for (size_t i = 0; i < 100; ++i) {
            auto buffer = std::make_unique<std::vector<uint8_t>>();
            buffer->reserve(BUFFER_SIZE);
            buffer_pool_.release(std::move(buffer));
        }
    }
    
    auto acquire_buffer() {
        return buffer_pool_.acquire();
    }
};

// 全局缓冲区池
thread_local BufferPool g_buffer_pool;
```

**零拷贝优化**

```cpp
// ✅ 使用writev减少内存拷贝
Task<size_t> Socket::write_vectored(const std::vector<iovec>& buffers) {
    size_t total_written = 0;
    
    while (total_written < total_size(buffers)) {
        auto result = co_await async_writev(fd_, buffers.data(), buffers.size());
        total_written += result;
        
        // 调整缓冲区偏移
        adjust_buffers(buffers, result);
    }
    
    co_return total_written;
}
```

### 2. 连接管理优化

**连接池复用**

```cpp
class ConnectionPool {
private:
    LockfreeQueue<std::unique_ptr<TcpConnection>> available_connections_;
    std::atomic<size_t> active_connections_{0};
    const size_t max_connections_;
    
public:
    Task<std::unique_ptr<TcpConnection>> acquire_connection(
        const std::string& host, uint16_t port) {
        
        // 尝试复用现有连接
        std::unique_ptr<TcpConnection> conn;
        if (available_connections_.dequeue(conn) && conn->is_connected()) {
            co_return std::move(conn);
        }
        
        // 创建新连接
        if (active_connections_ < max_connections_) {
            auto socket = std::make_unique<Socket>();
            co_await socket->connect(host, port);
            active_connections_++;
            co_return std::make_unique<TcpConnection>(std::move(socket));
        }
        
        throw std::runtime_error("Connection pool exhausted");
    }
};
```

**批量IO操作**

```cpp
// ✅ 批量读取减少系统调用
Task<std::vector<std::string>> TcpConnection::read_lines_batch(size_t max_lines) {
    std::vector<std::string> lines;
    lines.reserve(max_lines);
    
    // 一次性读取更多数据
    auto buffer = co_await read_bytes(64 * 1024);  // 64KB
    
    size_t pos = 0;
    while (pos < buffer.size() && lines.size() < max_lines) {
        auto line_end = std::find(buffer.begin() + pos, buffer.end(), '\n');
        if (line_end != buffer.end()) {
            lines.emplace_back(buffer.begin() + pos, line_end);
            pos = line_end - buffer.begin() + 1;
        } else {
            break;
        }
    }
    
    co_return lines;
}
```

## 内存优化

### 1. 内存池调优

**预分配策略**

```cpp
class AdaptiveMemoryPool {
private:
    struct PoolStats {
        std::atomic<size_t> allocations{0};
        std::atomic<size_t> deallocations{0};
        std::atomic<size_t> pool_hits{0};
        std::atomic<size_t> pool_misses{0};
    };
    
    PoolStats stats_;
    
public:
    void auto_tune() {
        double hit_rate = static_cast<double>(stats_.pool_hits) / 
                         (stats_.pool_hits + stats_.pool_misses);
        
        if (hit_rate < 0.8) {
            // 命中率低，增加池大小
            expand_pool(current_size_ * 1.5);
        } else if (hit_rate > 0.95 && get_memory_pressure() < 0.5) {
            // 命中率很高且内存压力不大，进一步扩展
            expand_pool(current_size_ * 1.2);
        }
    }
};
```

**NUMA感知分配**

```cpp
// NUMA节点感知的内存分配
class NumaAwareMemoryPool {
private:
    std::vector<MemoryPool<Task<void>>> per_node_pools_;
    
public:
    NumaAwareMemoryPool() {
        size_t num_nodes = get_numa_node_count();
        per_node_pools_.resize(num_nodes);
        
        for (size_t i = 0; i < num_nodes; ++i) {
            // 在特定NUMA节点上分配内存池
            per_node_pools_[i] = create_pool_on_node(i);
        }
    }
    
    Task<void>* allocate() {
        size_t current_node = get_current_numa_node();
        return per_node_pools_[current_node].allocate();
    }
};
```

### 2. 缓存优化

**数据结构布局优化**

```cpp
// ✅ 热数据紧密排列
struct HotColdData {
    // 热数据 - 频繁访问
    std::atomic<bool> is_ready{false};
    std::atomic<size_t> ref_count{0};
    void* result_ptr{nullptr};
    
    // 缓存行分割
    alignas(64) struct {
        // 冷数据 - 不常访问
        std::string debug_info;
        std::chrono::time_point<std::chrono::steady_clock> created_time;
        std::vector<std::string> metadata;
    } cold_data;
};
```

**预取优化**

```cpp
// 使用预取指令优化内存访问
template<typename Iterator>
void prefetch_range(Iterator begin, Iterator end) {
    for (auto it = begin; it != end; ++it) {
        __builtin_prefetch(&(*it), 0, 3);  // 预取到L1缓存
    }
}

Task<void> process_tasks_with_prefetch(const std::vector<Task<void>*>& tasks) {
    // 预取下一批任务数据
    if (tasks.size() > 8) {
        prefetch_range(tasks.begin() + 8, tasks.end());
    }
    
    for (auto* task : tasks) {
        co_await task->execute();
    }
}
```

## 性能监控和调试

### 1. 性能计数器

**内置性能统计**

```cpp
class PerformanceCounter {
private:
    std::atomic<uint64_t> total_operations_{0};
    std::atomic<uint64_t> total_time_ns_{0};
    mutable std::mutex stats_mutex_;
    std::vector<uint64_t> latency_histogram_;
    
public:
    void record_operation(uint64_t duration_ns) {
        total_operations_++;
        total_time_ns_ += duration_ns;
        
        // 记录延迟分布
        std::lock_guard<std::mutex> lock(stats_mutex_);
        size_t bucket = std::min(duration_ns / 1000, latency_histogram_.size() - 1);
        latency_histogram_[bucket]++;
    }
    
    double get_average_latency() const {
        return static_cast<double>(total_time_ns_) / total_operations_;
    }
    
    double get_throughput() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = now - start_time_;
        auto seconds = std::chrono::duration<double>(duration).count();
        return total_operations_ / seconds;
    }
};
```

### 2. 性能分析工具

**协程调度分析**

```cpp
#ifdef FLOWCORO_ENABLE_PROFILING
class CoroutineProfiler {
private:
    struct CoroutineStats {
        std::chrono::nanoseconds creation_time{0};
        std::chrono::nanoseconds execution_time{0};
        std::chrono::nanoseconds wait_time{0};
        size_t context_switches{0};
    };
    
    thread_local std::unordered_map<void*, CoroutineStats> coro_stats_;
    
public:
    void on_coroutine_created(void* coro_handle) {
        coro_stats_[coro_handle].creation_time = std::chrono::steady_clock::now();
    }
    
    void on_coroutine_suspended(void* coro_handle) {
        auto& stats = coro_stats_[coro_handle];
        stats.context_switches++;
        // 记录挂起时间...
    }
    
    void dump_stats() const {
        for (const auto& [handle, stats] : coro_stats_) {
            LOG_INFO("Coro {}: exec={}ns, wait={}ns, switches={}", 
                    handle, stats.execution_time.count(), 
                    stats.wait_time.count(), stats.context_switches);
        }
    }
};
#endif
```

## 基准测试

### 1. 微基准测试

**协程性能测试**

```cpp
void benchmark_coroutine_performance() {
    constexpr size_t NUM_ITERATIONS = 100000;
    
    // 测试协程创建开销
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        auto task = simple_coroutine();
        (void)task;  // 避免优化掉
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto creation_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "协程创建: " << creation_time.count() / NUM_ITERATIONS << "ns/op\n";
    
    // 测试协程执行开销
    std::vector<Task<int>> tasks;
    tasks.reserve(NUM_ITERATIONS);
    
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        tasks.emplace_back(compute_coroutine(i));
    }
    
    for (auto& task : tasks) {
        auto result = task.get();
        (void)result;
    }
    end = std::chrono::high_resolution_clock::now();
    
    auto execution_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "协程执行: " << execution_time.count() / NUM_ITERATIONS << "ns/op\n";
}
```

### 2. 端到端基准测试

**网络吞吐量测试**

```cpp
Task<void> benchmark_network_throughput() {
    auto& loop = GlobalEventLoop::get();
    TcpServer server(&loop);
    
    // 设置简单的echo处理器
    server.set_connection_handler([](auto socket) -> Task<void> {
        auto conn = std::make_unique<TcpConnection>(std::move(socket));
        
        while (true) {
            try {
                auto data = co_await conn->read_bytes(1024);
                co_await conn->write_bytes(data);
            } catch (...) {
                break;
            }
        }
    });
    
    co_await server.listen("127.0.0.1", 8080);
    
    // 启动多个客户端进行压力测试
    std::vector<Task<void>> client_tasks;
    for (size_t i = 0; i < 100; ++i) {
        client_tasks.emplace_back(client_load_test("127.0.0.1", 8080));
    }
    
    // 等待所有客户端完成
    for (auto& task : client_tasks) {
        co_await task;
    }
}
```

## 编译器优化

### 1. 编译选项

**推荐的编译参数**

```cmake
# Release构建优化
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -flto")

# 协程特定优化
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")

# 链接时优化
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
```

### 2. Profile导向优化 (PGO)

**使用PGO提升性能**

```bash
# 第一阶段：生成性能分析数据
g++ -O3 -fprofile-generate -fcoroutines main.cpp -o app
./app  # 运行典型工作负载

# 第二阶段：使用性能数据优化
g++ -O3 -fprofile-use -fcoroutines main.cpp -o app_optimized
```

通过遵循这些性能调优指南，你可以充分发挥FlowCoro的性能优势，构建高效的异步应用程序。记住要定期进行性能测试和分析，根据实际工作负载调整优化策略。
