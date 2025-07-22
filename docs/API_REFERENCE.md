# FlowCoro v2.3.0 API 参考手册

## 🆕 v2.3.0 更新亮点

### Task接口统一
- **统一设计**: SafeTask现在是Task的别名，消除接口混淆
- **向后兼容**: 所有现有代码无需修改
- **零开销抽象**: 编译时别名，无运行时开销
- **API一致性**: 两种风格的方法都可用

### 性能基准测试
基于实际测试的性能数据：
- **协程创建**: ~256ns/op (1000次创建测试)
- **协程执行**: ~9ns/op (1000次执行测试)
- **无锁队列操作**: ~165ns/op (1M次操作测试)
- **内存池分配**: ~364ns/op (10K次分配测试)
- **日志记录**: ~443ns/op (10K次日志测试)

### 统一接口示例
```cpp
// SafeTask基础使用
auto task = compute_async();           // 创建协程
auto result = task.get_result();       // 同步获取结果
task.start([](auto r) { /* ... */ }); // 异步启动

// 协程组合
auto combined = chain_computation(5);  // 链式协程
auto result = co_await combined;       // 等待完成
```

## 📋 目录

### 核心模块
- [1. 协程核心 (core.h)](#1-协程核心-coreh) - Task、SafeTask、生命周期管理、协程工具函数
- [2. 无锁数据结构 (lockfree.h)](#2-无锁数据结构-lockfreeh) - 队列、栈、环形缓冲区  
- [3. 线程池 (thread_pool.h)](#3-线程池-thread_poolh) - 全局线程池、工作线程
- [4. 网络IO (net.h)](#4-网络io-neth) - Socket、TCP连接、事件循环、HTTP客户端

### 系统模块  
- [5. 日志系统 (logger.h)](#5-日志系统-loggerh) - 异步日志记录器
- [6. 缓冲区管理 (buffer.h)](#6-缓冲区管理-bufferh) - 对齐分配器、缓存友好缓冲区
- [7. 内存管理 (memory.h)](#7-内存管理-memoryh) - 内存池、对象池

### 高级功能
- [8. SafeTask协程包装器](#8-safetask协程包装器) - RAII设计的安全协程包装器
- [9. 生命周期管理](#9-生命周期管理-已整合到coreh) - CoroutineScope、状态管理
- [10. 便利功能](#10-便利功能-coreh) - 转换函数、工具函数

### 其他
- [错误处理](#错误处理) - 异常类型、错误代码
- [性能提示](#性能提示) - 最佳实践、优化建议
- [编译和链接](#编译和链接) - CMake配置、编译器要求

## 概述

FlowCoro 提供了一套完整的现代C++20协程编程接口，包括协程核心、无锁数据结构、线程池、网络IO、日志系统和内存管理等模块。

## 核心模块

### 1. 协程核心 (core.h)

FlowCoro v2.3.0 提供了两种协程任务设计：**Task<T>**用于传统协程编程，**SafeTask<T>**用于现代RAII设计。

#### Task<T> - 传统协程类型
FlowCoro的传统协程任务模板类，提供完整的生命周期管理和Promise风格API。

```cpp
template<typename T>
struct Task {
    struct promise_type {
        // 生命周期管理
        std::atomic<bool> is_cancelled_{false};
        std::atomic<bool> is_destroyed_{false};
        std::chrono::steady_clock::time_point creation_time_;
        
        // Promise接口
        Task get_return_object();
        std::suspend_never initial_suspend() noexcept;
        std::suspend_always final_suspend() noexcept;
        void return_value(T v) noexcept;
        void unhandled_exception();
        
        // 取消支持
        void request_cancellation();
        bool is_cancelled() const;
        bool is_destroyed() const;
    };
    
    // 状态查询 - Promise风格API
    bool is_pending() const noexcept;      // 任务进行中
    bool is_settled() const noexcept;      // 任务已结束
    bool is_fulfilled() const noexcept;    // 任务成功完成
    bool is_rejected() const noexcept;     // 任务失败或取消
    
    // 生命周期管理
    void cancel();
    bool is_cancelled() const;
    std::chrono::milliseconds get_lifetime() const;
    bool is_active() const;
    
    // 任务执行
    T get();
    
    // Awaitable接口
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<> waiting_handle);
    T await_resume();
};
```

**使用场景：**
- ✅ 日常协程编程，兼容现有代码
- ✅ 需要Promise风格状态查询
- ✅ 基本的取消和生命周期管理
- ✅ RPC、数据库、网络等模块集成

#### SafeTask<T> - 现代RAII协程包装器
基于async_simple最佳实践的协程包装器，提供RAII资源管理和异常安全。

```cpp
template<typename T = void>
class SafeTask {
public:
    using promise_type = detail::SafeTaskPromise<T>;
    
    // RAII构造与析构
    explicit SafeTask(Handle handle) noexcept;
    SafeTask(SafeTask&& other) noexcept;
    ~SafeTask();
    
    // 状态查询
    bool is_ready() const noexcept;
    
    // 同步获取结果
    T get_result() requires(!std::is_void_v<T>);
    void get_result() requires(std::is_void_v<T>);
    
    // 异步启动
    template<typename F>
    void start(F&& callback);
    
    // Awaitable接口
    auto operator co_await() &&;
};
```

**技术特性：**
- ✅ RAII自动资源管理
- ✅ 异常安全的Promise设计  
- ✅ 线程安全协程管理（防止跨线程段错误）
- ✅ CoroutineScope生命周期管理
- ✅ 统一的awaiter接口

**使用建议：**
- **推荐新代码使用SafeTask** - 更安全，符合现代C++设计
- **现有代码可继续使用Task** - 向后兼容，功能完整

#### 特化版本
- `Task<void>` - 无返回值的Task协程
- `SafeTask<void>` - 无返回值的SafeTask协程
- `Task<Result<T,E>>` - 返回Result的Task协程
- `Task<std::unique_ptr<T>>` - 返回智能指针的Task协程

### 使用建议

#### 🚀 推荐使用 SafeTask<T> 的场景（新代码）

```cpp
// ✅ RAII设计的协程函数
SafeTask<std::string> fetch_user_data(int user_id) {
    // 自动资源管理，异常安全
    co_return "user_data";
}

// ✅ 线程安全协程管理
SafeTask<void> background_task() {
    co_await sleep_for(std::chrono::milliseconds(100));
    // 自动防止跨线程协程恢复导致的段错误
    // SafeCoroutineHandle会检查线程ID并阻止不安全的恢复
}
```

#### ⚡ 使用 Task<T> 的场景（兼容性需求）

```cpp
// ✅ 与现有代码兼容
Task<int> compute_legacy() {
    co_return 42;
}

// ✅ 需要Promise风格状态查询
Task<std::string> http_request(const std::string& url) {
    auto task = make_http_request(url);
    if (task.is_pending()) {
        // 任务进行中
    }
    co_return co_await task;
}

#### AsyncPromise<T>
协程promise类型，管理协程的生命周期和结果。

```cpp
template<typename T>
class AsyncPromise {
public:
    // 标准协程接口
    Task<T> get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_always final_suspend() noexcept;
    void unhandled_exception();
    
    // 返回值设置
    void return_value(const T& value);  // Task<T>
    void return_void();                 // Task<void>
};
```

#### 协程工具函数

```cpp
// 异步睡眠
auto sleep_for(std::chrono::milliseconds duration) -> SleepAwaiter;

// 等待所有任务完成
template<typename... Tasks>
auto when_all(Tasks&&... tasks) -> WhenAllAwaiter<Tasks...>;

// 同步等待协程完成
template<typename T>
T sync_wait(Task<T>&& task);

template<typename Func>
auto sync_wait(Func&& func) -> decltype(sync_wait(func()));

// SafeTask便利函数  
template<typename T>
auto make_safe_task(Task<T>&& task) -> SafeTask<T>;

// 协程组合器
template<typename... Tasks>
auto when_all(Tasks&&... tasks);

// 简化版超时支持
template<typename T>
auto make_timeout_task(Task<T>&& task, std::chrono::milliseconds timeout) -> Task<T>;
```

### 2. 无锁数据结构 (lockfree.h)

#### LockfreeQueue<T>
无锁队列，支持多生产者多消费者。

```cpp
template<typename T>
class LockfreeQueue {
public:
    LockfreeQueue(size_t capacity = 1024);
    
    // 入队操作
    bool enqueue(const T& item);
    bool enqueue(T&& item);
    
    // 出队操作
    bool dequeue(T& item);
    
    // 状态查询
    bool empty() const;
    size_t size() const;
    size_t capacity() const;
};
```

#### LockfreeStack<T>
无锁栈，支持高并发访问。

```cpp
template<typename T>
class LockfreeStack {
public:
    LockfreeStack();
    
    // 栈操作
    void push(const T& item);
    void push(T&& item);
    bool pop(T& item);
    
    // 状态查询
    bool empty() const;
    size_t size() const;
};
```

#### RingBuffer<T>
环形缓冲区，缓存友好的高性能缓冲。

```cpp
template<typename T>
class RingBuffer {
public:
    RingBuffer(size_t capacity);
    
    // 读写操作
    bool write(const T& item);
    bool read(T& item);
    
    // 批量操作
    size_t write_batch(const T* items, size_t count);
    size_t read_batch(T* items, size_t count);
    
    // 状态查询
    size_t available_read() const;
    size_t available_write() const;
};
```

### 3. 线程池 (thread_pool.h)

#### GlobalThreadPool
全局线程池单例，提供工作窃取调度。

```cpp
class GlobalThreadPool {
public:
    // 获取全局实例
    static GlobalThreadPool& get();
    
    // 提交任务
    template<typename F>
    auto submit(F&& func) -> std::future<decltype(func())>;
    
    // 协程任务提交
    template<typename F>
    Task<void> submit_coro(F&& func);
    
    // 线程池管理
    void resize(size_t num_threads);
    size_t size() const;
    
    // 统计信息
    size_t pending_tasks() const;
    size_t completed_tasks() const;
};
```

#### WorkerThread
工作线程类，执行任务和协程。

```cpp
class WorkerThread {
public:
    WorkerThread(size_t id);
    
    // 线程控制
    void start();
    void stop();
    void join();
    
    // 任务管理
    bool try_steal_task();
    void add_task(std::function<void()> task);
    
    // 状态查询
    bool is_running() const;
    size_t get_id() const;
};
```

### 4. 网络IO (net.h)

#### Socket
异步Socket封装，支持协程化IO操作。

```cpp
class Socket {
public:
    Socket(int fd = -1);
    ~Socket();
    
    // 连接操作
    Task<void> connect(const std::string& host, uint16_t port);
    Task<void> bind(const std::string& host, uint16_t port);
    Task<void> listen(int backlog = 128);
    Task<std::unique_ptr<Socket>> accept();
    
    // 数据传输
    Task<size_t> read(void* buffer, size_t size);
    Task<size_t> write(const void* buffer, size_t size);
    Task<std::string> read_line();
    Task<void> write_all(const void* buffer, size_t size);
    
    // 状态管理
    void close();
    bool is_valid() const;
    int get_fd() const;
    
    // 选项设置
    void set_non_blocking(bool non_blocking = true);
    void set_reuse_addr(bool reuse = true);
    void set_no_delay(bool no_delay = true);
};
```

#### TcpConnection
TCP连接封装，提供高级别的数据传输接口。

```cpp
class TcpConnection {
public:
    TcpConnection(std::unique_ptr<Socket> socket);
    
    // 数据传输
    Task<std::string> read_line();
    Task<std::vector<uint8_t>> read_bytes(size_t count);
    Task<void> write(const std::string& data);
    Task<void> write_bytes(const std::vector<uint8_t>& data);
    Task<void> flush();
    
    // 连接管理
    void close();
    bool is_connected() const;
    std::string get_remote_address() const;
    uint16_t get_remote_port() const;
};
```

#### TcpServer
TCP服务器，支持高并发连接处理。

```cpp
class TcpServer {
public:
    using ConnectionHandler = std::function<Task<void>(std::unique_ptr<Socket>)>;
    
    TcpServer(EventLoop* loop);
    
    // 服务器管理
    Task<void> listen(const std::string& host, uint16_t port);
    void stop();
    
    // 连接处理
    void set_connection_handler(ConnectionHandler handler);
    
    // 状态查询
    bool is_listening() const;
    uint16_t get_port() const;
    size_t get_connection_count() const;
};
```

#### EventLoop
事件循环，基于epoll的高性能IO多路复用。

```cpp
class EventLoop {
public:
    EventLoop();
    ~EventLoop();
    
    // 事件循环控制
    Task<void> run();
    void stop();
    
    // 事件注册
    void add_socket(int fd, uint32_t events, std::function<void()> callback);
    void remove_socket(int fd);
    void modify_socket(int fd, uint32_t events);
    
    // 定时器
    void add_timer(std::chrono::milliseconds delay, std::function<void()> callback);
    
    // 状态查询
    bool is_running() const;
    size_t get_socket_count() const;
};

// 全局事件循环
class GlobalEventLoop {
public:
    static EventLoop& get();
};
```

#### HttpClient
现代HTTP客户端，支持异步请求和协程化接口。

```cpp
namespace flowcoro::net {

// HTTP方法
enum class HttpMethod {
    GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH
};

// HTTP响应
struct HttpResponse {
    int status_code;
    std::string status_message;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds duration;
    
    bool is_success() const { return status_code >= 200 && status_code < 300; }
};

// HTTP客户端
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // 异步HTTP请求
    Task<HttpResponse> request(
        HttpMethod method,
        const std::string& url,
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );
    
    // 便捷方法
    Task<HttpResponse> get(const std::string& url, 
                          const std::unordered_map<std::string, std::string>& headers = {});
    Task<HttpResponse> post(const std::string& url, const std::string& body,
                           const std::unordered_map<std::string, std::string>& headers = {});
    Task<HttpResponse> put(const std::string& url, const std::string& body,
                          const std::unordered_map<std::string, std::string>& headers = {});
    Task<HttpResponse> delete_request(const std::string& url,
                                     const std::unordered_map<std::string, std::string>& headers = {});
    
    // 配置选项
    void set_timeout(std::chrono::milliseconds timeout);
    void set_user_agent(const std::string& user_agent);
    void set_follow_redirects(bool follow);
    void set_max_redirects(int max_redirects);
};

}
```

### 5. 日志系统 (logger.h)

#### Logger
异步日志记录器，支持多级别、高性能日志记录。

```cpp
class Logger {
public:
    enum class Level {
        DEBUG, INFO, WARN, ERROR, FATAL
    };
    
    // 日志记录
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);
    
    // 格式化日志
    template<typename... Args>
    void log(Level level, const std::string& format, Args&&... args);
    
    // 配置管理
    void set_level(Level level);
    void set_output_file(const std::string& filename);
    void set_async(bool async = true);
    
    // 统计信息
    size_t get_log_count() const;
    size_t get_bytes_written() const;
};

// 全局日志器
class GlobalLogger {
public:
    static Logger& get();
};

// 便捷宏
#define LOG_DEBUG(msg) GlobalLogger::get().debug(msg)
```

### 9. 生命周期管理 (已整合到core.h)

原独立的生命周期管理功能现已完全整合到core.h中。

#### coroutine_state
协程状态枚举，表示协程在不同执行阶段的状态。

```cpp
enum class coroutine_state {
    created,    // 刚创建，尚未开始执行
    running,    // 正在运行
    suspended,  // 已挂起等待
    completed,  // 正常完成
    destroyed,  // 已销毁（发生异常）
    cancelled   // 被取消
};
```

#### coroutine_state_manager
协程状态管理器，负责跟踪和转换协程状态。

```cpp
class coroutine_state_manager {
public:
    // 尝试状态转换
    bool try_transition(coroutine_state from, coroutine_state to) noexcept;
    
    // 强制状态转换
    void force_transition(coroutine_state to) noexcept;
    
    // 获取当前状态
    coroutine_state get_state() const noexcept;
    
    // 检查是否处于某个状态
    bool is_state(coroutine_state expected) const noexcept;
    
    // 获取运行时长
    std::chrono::milliseconds get_lifetime() const noexcept;
    
    // 检查是否活跃
    bool is_active() const noexcept;
};
```

#### cancellation_token
取消令牌，用于在协程执行过程中检查和处理取消请求。

```cpp
class cancellation_token {
public:
    cancellation_token() = default;
    
    // 检查是否已取消
    bool is_cancelled() const noexcept;
    
    // 如果已取消则抛出异常
    void throw_if_cancelled() const;
    
    // 注册取消回调
    template<typename Callback>
    void register_callback(Callback&& callback);
    
    // 创建超时令牌
    static cancellation_token create_timeout(std::chrono::milliseconds timeout);
    
    // 创建组合令牌
    static cancellation_token combine(const cancellation_token& a, const cancellation_token& b);
};
```

#### cancellation_source
取消源，用于请求取消关联的令牌。

```cpp
class cancellation_source {
public:
    cancellation_source();
    
    // 获取关联的令牌
    cancellation_token get_token() const;
    
    // 请求取消
    void request_cancellation() noexcept;
    
    // 检查是否已取消
    bool is_cancellation_requested() const noexcept;
};
```

#### safe_coroutine_handle
安全的协程句柄包装器，自动处理协程析构和状态管理。

```cpp
class safe_coroutine_handle {
public:
    safe_coroutine_handle() = default;
    explicit safe_coroutine_handle(std::coroutine_handle<> handle);
    
    // 移动语义
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept;
    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept;
    
    // 禁止拷贝
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    // 析构函数自动处理资源释放
    ~safe_coroutine_handle();
    
    // 操作句柄
    bool valid() const noexcept;
    void resume();
    bool done() const;
    void* address() const;
    
    // 获取promise
    template<typename Promise>
    Promise& promise();
};
```

### 线程安全 (SafeCoroutineHandle)

FlowCoro v2.3.1 新增了专门的线程安全协程句柄管理，解决跨线程协程恢复导致的段错误问题。

#### SafeCoroutineHandle - 线程安全协程句柄

```cpp
class SafeCoroutineHandle {
private:
    std::shared_ptr<std::atomic<std::coroutine_handle<>>> handle_;
    std::shared_ptr<std::atomic<bool>> destroyed_;
    std::thread::id creation_thread_id_;  // 创建线程ID
    
public:
    explicit SafeCoroutineHandle(std::coroutine_handle<> h);
    
    // 线程安全的协程恢复
    void resume() {
        // 自动检测跨线程调用
        if (std::this_thread::get_id() != creation_thread_id_) {
            std::cerr << "[FlowCoro] Cross-thread coroutine resume blocked to prevent segfault."
                      << std::endl;
            return; // 阻止跨线程恢复
        }
        // 安全恢复协程...
    }
    
    ~SafeCoroutineHandle();
};
```

**线程安全特性：**
- ✅ **跨线程检测**：自动检测并阻止跨线程协程恢复
- ✅ **段错误防护**：防止经典的跨线程协程段错误
- ✅ **调试友好**：提供详细的线程ID信息用于调试
- ✅ **自动管理**：RAII资源管理，自动清理

**使用场景：**
```cpp
// ✅ 线程安全睡眠
Task<void> safe_sleep_task() {
    co_await sleep_for(std::chrono::milliseconds(100));
    // SafeCoroutineHandle会自动防止跨线程恢复
}

// ✅ 线程池任务
Task<int> computation_task() {
    // 协程可能在不同线程中执行
    // SafeCoroutineHandle确保恢复操作的线程安全
    co_return 42;
}
```

### 10. 增强Task (flowcoro_enhanced_task.h)

增强版协程Task实现，完全兼容原Task接口，并扩展了生命周期管理和取消功能。

```cpp
template<typename T>
class enhanced_task {
public:
    // 构造函数
    explicit enhanced_task(safe_coroutine_handle handle);
    
    // 移动语义
    enhanced_task(enhanced_task&& other) noexcept;
    enhanced_task& operator=(enhanced_task&& other) noexcept;
    
    // 禁止拷贝
    enhanced_task(const enhanced_task&) = delete;
    enhanced_task& operator=(const enhanced_task&) = delete;
    
    // 从普通Task创建
    static enhanced_task from_task(Task<T>&& task, cancellation_token token = {});
    
    // 获取结果（兼容原Task接口）
    T get();
    
    // 可等待接口（兼容原Task接口）
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<> waiting_handle);
    T await_resume();
    
    // 新增功能：取消支持
    void cancel();
    bool is_cancelled() const;
    
    // 新增功能：完整状态查询
    bool is_ready() const;
    coroutine_state get_state() const;
    std::chrono::milliseconds get_lifetime() const;
    bool is_active() const;
    
    // 新增功能：设置取消令牌
    void set_cancellation_token(cancellation_token token);
    
    // 调试信息
    void* get_handle_address() const;
    bool is_handle_valid() const;
};
```

### 11. 便利功能 (core.h)

提供便捷的协程创建和管理功能。

```cpp
// 将现有Task转换为增强Task
template<typename T>
auto make_enhanced(Task<T>&& task) -> enhanced_task<T>;

// 创建可取消任务
template<typename T>
auto make_cancellable_task(Task<T> task) -> enhanced_task<T>;

// 创建带超时的任务
template<typename T>
auto make_timeout_task(Task<T>&& task, std::chrono::milliseconds timeout) -> enhanced_task<T>;

// 异步睡眠
auto sleep_for(std::chrono::milliseconds duration) -> SleepAwaiter;

// 等待所有任务完成
template<typename... Tasks>
auto when_all(Tasks&&... tasks) -> WhenAllAwaiter<Tasks...>;

// 同步等待协程完成
template<typename T>
T sync_wait(Task<T>&& task);

// 性能报告
inline void print_performance_report();
```

### 6. 缓冲区管理 (buffer.h)

#### AlignedAllocator
内存对齐分配器，确保数据按照指定边界对齐以优化缓存性能。

```cpp
template<size_t Alignment = CACHE_LINE_SIZE>
class AlignedAllocator {
public:
    // 分配对齐内存
    static void* allocate(size_t size);
    
    // 释放内存
    static void deallocate(void* ptr);
};
```

#### CacheFriendlyRingBuffer
缓存友好的循环缓冲区，避免false sharing，适用于高性能生产者-消费者场景。

```cpp
template<typename T, size_t Capacity>
class CacheFriendlyRingBuffer {
public:
    CacheFriendlyRingBuffer();
    
    // 写入操作
    bool try_write(const T& item);
    bool try_write(T&& item);
    
    // 读取操作
    bool try_read(T& item);
    
    // 批量操作
    size_t write_batch(const T* items, size_t count);
    size_t read_batch(T* items, size_t count);
    
    // 状态查询
    bool empty() const;
    bool full() const;
    size_t size() const;
    static constexpr size_t capacity() { return Capacity; }
};
```

#### CacheFriendlyMemoryPool
缓存友好的内存池，针对特定大小的内存块进行优化。

```cpp
template<size_t BlockSize>
class CacheFriendlyMemoryPool {
public:
    CacheFriendlyMemoryPool(size_t initial_blocks = 1024);
    ~CacheFriendlyMemoryPool();
    
    // 内存分配和释放
    void* allocate();
    void deallocate(void* ptr);
    
    // 统计信息
    struct PoolStats {
        size_t total_blocks;
        size_t allocated_blocks;
        size_t free_blocks;
        double hit_rate;
    };
    
    PoolStats get_stats() const;
    void reset_stats();
};
```

#### StringBuffer
高性能字符串缓冲区，支持高效的字符串拼接和格式化。

```cpp
class StringBuffer {
public:
    StringBuffer(size_t initial_capacity = 4096);
    
    // 字符串操作
    StringBuffer& append(const std::string& str);
    StringBuffer& append(const char* str);
    StringBuffer& append(char c);
    
    // 格式化操作
    template<typename... Args>
    StringBuffer& format(const std::string& fmt, Args&&... args);
    
    // 访问和转换
    const char* c_str() const;
    std::string to_string() const;
    size_t size() const;
    size_t capacity() const;
    
    // 缓冲区管理
    void clear();
    void reserve(size_t new_capacity);
    void shrink_to_fit();
};
```

### 7. 内存管理 (memory.h)

#### MemoryPool
高性能内存池，减少动态内存分配开销。

```cpp
template<typename T>
class MemoryPool {
public:
    MemoryPool(size_t initial_size = 1024);
    ~MemoryPool();
    
    // 内存分配
    T* allocate();
    void deallocate(T* ptr);
    
    // 批量分配
    std::vector<T*> allocate_batch(size_t count);
    void deallocate_batch(const std::vector<T*>& ptrs);
    
    // 统计信息
    size_t allocated_count() const;
    size_t available_count() const;
    size_t total_capacity() const;
};
```

#### ObjectPool
对象池，支持对象复用和自动构造析构。

```cpp
template<typename T>
class ObjectPool {
public:
    ObjectPool(size_t initial_size = 100);
    
    // 对象获取和释放
    template<typename... Args>
    std::unique_ptr<T> acquire(Args&&... args);
    void release(std::unique_ptr<T> obj);
    
    // 池管理
    void resize(size_t new_size);
    void clear();
    
    // 统计信息
    size_t available_objects() const;
    size_t total_objects() const;
};
```

#### CacheLineBuffer
缓存行对齐的缓冲区，优化内存访问性能。

```cpp
template<typename T, size_t Size>
class CacheLineBuffer {
public:
    CacheLineBuffer();
    
    // 数据访问
    T& operator[](size_t index);
    const T& operator[](size_t index) const;
    T* data();
    const T* data() const;
    
    // 容量信息
    constexpr size_t size() const { return Size; }
    constexpr size_t capacity() const { return Size; }
    
    // 缓存友好操作
    void prefetch(size_t index) const;
    void flush() const;
};
```

## 错误处理

### 异常类型

```cpp
// 基础异常类
class FlowCoroException : public std::exception {
public:
    FlowCoroException(const std::string& message);
    const char* what() const noexcept override;
};

// 网络异常
class NetworkException : public FlowCoroException {
public:
    NetworkException(const std::string& message, int error_code = 0);
    int get_error_code() const;
};

// 协程异常
class CoroutineException : public FlowCoroException {
public:
    CoroutineException(const std::string& message);
};

// 内存异常
class MemoryException : public FlowCoroException {
public:
    MemoryException(const std::string& message);
};
```

### 错误代码

```cpp
enum class ErrorCode {
    SUCCESS = 0,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    NETWORK_ERROR,
    TIMEOUT,
    COROUTINE_ERROR,
    FILE_ERROR,
    PERMISSION_DENIED
};
```

## 性能提示

### 协程最佳实践

1. **选择合适的Task类型**：
   - 默认使用`Task<T>` - 性能最优，满足99%场景
   - 仅在需要精确状态控制时使用`enhanced_task<T>`
   
2. **使用移动语义**：协程参数优先使用移动语义

3. **避免阻塞调用**：在协程中避免使用阻塞的同步API

4. **合理使用co_await**：只对真正的异步操作使用co_await

5. **资源管理**：使用RAII和智能指针管理资源

### 内存优化

1. **使用内存池**：频繁分配的对象使用MemoryPool
2. **缓存对齐**：关键数据结构使用缓存行对齐
3. **避免拷贝**：使用引用和移动语义减少不必要的拷贝
4. **预分配**：提前分配足够的缓冲区空间

### 网络编程建议

1. **连接复用**：建立长连接避免频繁建立断开
2. **批量操作**：使用批量读写减少系统调用
3. **缓冲管理**：合理设置读写缓冲区大小
4. **错误处理**：妥善处理网络异常和断线重连

## 编译和链接

### CMake配置

```cmake
find_package(FlowCoro REQUIRED)
target_link_libraries(your_target PRIVATE flowcoro_net)
target_compile_features(your_target PRIVATE cxx_std_20)
```

### 编译器要求

- **GCC 11+** 或 **Clang 13+**
- **C++20协程支持**
- **Linux epoll支持**

### 链接库

- `flowcoro_net` - 完整的FlowCoro库（包含网络支持）
- `pthread` - 线程支持
- `rt` - 实时扩展（定时器支持）

## 版本兼容性

| FlowCoro版本 | C++标准 | 编译器要求 | 平台支持 |
|-------------|---------|-----------|----------|
| 2.0+ | C++20 | GCC 11+, Clang 13+ | Linux |
| 1.x | C++17 | GCC 9+, Clang 11+ | Linux, Windows |

## 相关资源

- [学习指南](LEARNING_GUIDE.md) - 从入门到精通
- [性能调优](PERFORMANCE_GUIDE.md) - 高性能编程技巧
- [网络编程](NETWORK_GUIDE.md) - 异步网络开发指南
- [项目示例](../examples/) - 完整的使用示例
