# FlowCoro API 参考手册

## 概述

FlowCoro 提供了一套完整的现代C++20协程编程接口，包括协程核心、无锁数据结构、线程池、网络IO、日志系统和内存管理等模块。

## 核心模块

### 1. 协程核心 (core.h)

#### Task<T>
协程任务模板类，支持co_await语法。

```cpp
template<typename T>
class Task {
public:
    // 构造函数
    Task(std::coroutine_handle<Promise> handle);
    
    // 获取结果 (阻塞等待)
    T get();
    
    // 异步等待 (协程中使用)
    auto operator co_await();
    
    // 检查是否完成
    bool is_ready() const;
};
```

**特化版本：**
- `Task<void>` - 无返回值的协程
- `Task<std::unique_ptr<T>>` - 返回智能指针的协程

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
Task<void> sleep_for(std::chrono::milliseconds duration);

// 切换到指定线程池执行
Task<void> switch_to_thread_pool();

// 协程异常处理
void handle_coroutine_exception(std::exception_ptr eptr);
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
#define LOG_INFO(msg)  GlobalLogger::get().info(msg)
#define LOG_WARN(msg)  GlobalLogger::get().warn(msg)
#define LOG_ERROR(msg) GlobalLogger::get().error(msg)
#define LOG_FATAL(msg) GlobalLogger::get().fatal(msg)
```

### 6. 内存管理 (memory.h)

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

1. **使用移动语义**：协程参数优先使用移动语义
2. **避免阻塞调用**：在协程中避免使用阻塞的同步API
3. **合理使用co_await**：只对真正的异步操作使用co_await
4. **资源管理**：使用RAII和智能指针管理资源

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
