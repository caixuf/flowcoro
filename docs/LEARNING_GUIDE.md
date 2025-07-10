# FlowCoro 学习指南

## 🎯 学习目标
掌握现代C++20协程编程，理解无锁数据结构设计，能够独立开发高性能异步应用。

## 📖 学习路径

### Phase 1: 基础知识巩固 (1-2周)

#### 1.1 现代C++特性回顾
```cpp
// 必须熟练掌握的概念
- RAII与智能指针
- 移动语义与完美转发  
- 模板编程基础
- lambda表达式
- std::function与函数对象
```

**推荐资源**：
- 《Effective Modern C++》第1-4章
- [cppreference.com](https://en.cppreference.com/) 

#### 1.2 多线程编程基础
```cpp
// 关键概念
- std::thread生命周期管理
- 互斥锁与条件变量
- std::atomic基础操作
- 线程池基本原理
```

**实践项目**：实现一个简单的线程池

### Phase 2: C++20协程深入 (2-3周)

#### 2.1 协程三剑客
```cpp
// co_yield: 生成器模式
auto fibonacci() -> std::generator<int> {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto tmp = a + b;
        a = b; b = tmp;
    }
}

// co_await: 异步等待
auto read_file_async(const std::string& path) -> Task<std::string> {
    auto content = co_await async_read(path);
    co_return content;
}

// co_return: 协程返回
auto compute() -> Task<int> {
    co_return 42;
}
```

#### 2.2 协程机制深入
```cpp
// Promise类型设计
struct TaskPromise {
    Task get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_never final_suspend() noexcept;
    void unhandled_exception();
    void return_value(T value);
};

// Awaiter概念实现  
struct FileAwaiter {
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<>);
    std::string await_resume();
};
```

**学习重点**：
- 协程栈帧的生命周期
- Promise/Awaiter的交互机制
- 协程句柄的管理

#### 2.3 实践项目
实现一个简单的协程任务调度器：
```cpp
class SimpleScheduler {
    std::queue<std::coroutine_handle<>> ready_queue_;
public:
    void schedule(std::coroutine_handle<> h);
    void run_until_complete();
};
```

### Phase 3: 无锁编程掌握 (3-4周)

#### 3.1 内存模型理解
```cpp
// 内存序从简单到复杂
std::memory_order_relaxed  // 最宽松，只保证原子性
std::memory_order_acquire  // 获取语义，防止后续操作重排到前面
std::memory_order_release  // 释放语义，防止前面操作重排到后面  
std::memory_order_seq_cst  // 顺序一致性，最严格
```

#### 3.2 经典无锁算法
```cpp
// 1. 无锁栈 (Treiber Stack)
template<typename T>
class LockFreeStack {
    struct Node { T data; Node* next; };
    std::atomic<Node*> head_{nullptr};
    
public:
    void push(T item) {
        Node* new_node = new Node{std::move(item), head_.load()};
        while (!head_.compare_exchange_weak(new_node->next, new_node));
    }
    
    bool pop(T& result) {
        Node* old_head = head_.load();
        while (old_head && !head_.compare_exchange_weak(old_head, old_head->next));
        if (old_head) {
            result = std::move(old_head->data);
            delete old_head;
            return true;
        }
        return false;
    }
};

// 2. 无锁队列 (Michael & Scott Queue)  
template<typename T>
class LockFreeQueue {
    struct Node { std::atomic<T*> data; std::atomic<Node*> next; };
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
    // 实现比栈复杂得多...
};
```

#### 3.3 常见陷阱与解决方案
```cpp
// ABA问题示例
Node* old_head = head_.load();        // 读取A
// 此时其他线程: A->B, 然后B被删除, A被重新插入
if (head_.compare_exchange_weak(old_head, old_head->next)) {
    // 危险！old_head->next可能已经无效
}

// 解决方案：带版本号的指针
struct VersionedPointer {
    Node* ptr;
    uint64_t version;
};
```

### Phase 4: FlowCoro源码剖析 (2-3周)

#### 4.1 项目架构理解
```
FlowCoro架构层次：
┌─────────────────────────────────┐
│        应用层 (用户代码)          │  
├─────────────────────────────────┤
│     协程调度层 (CoroTask)        │  ← 学习重点1
├─────────────────────────────────┤  
│   执行层 (ThreadPool + Scheduler) │  ← 学习重点2
├─────────────────────────────────┤
│    数据结构层 (LockFree DS)      │  ← 学习重点3
├─────────────────────────────────┤
│     基础设施层 (Logger/Memory)    │
└─────────────────────────────────┘
```

#### 4.2 源码阅读顺序
1. **基础数据结构** (`lockfree.h`)
   - 先理解Queue、Stack的实现
   - 重点关注内存序的使用

2. **线程池实现** (`thread_pool.h`)
   - 理解任务调度机制
   - 学习工作窃取算法

3. **协程核心** (`core.h`)
   - CoroTask的Promise/Awaiter实现
   - 协程与线程池的协作机制

4. **高级特性** (其他头文件)
   - 内存池的设计思路
   - 异步日志的实现

#### 4.3 调试与学习技巧
```cpp
// 添加调试信息帮助理解
#define DEBUG_CORO(msg, ...) \
    printf("[CORO %p] " msg "\n", std::coroutine_handle<>::from_address(this), ##__VA_ARGS__)

// 在关键点添加日志
void await_suspend(std::coroutine_handle<> h) {
    DEBUG_CORO("Suspending coroutine, will resume in thread pool");
    GlobalThreadPool::get().enqueue_void([h]() {
        DEBUG_CORO("Resuming coroutine in worker thread %zu", std::this_thread::get_id());
        h.resume();
    });
}
```

## 🛠️ 实践建议

### 渐进式学习法
1. **先跑通现有代码**：确保能编译运行所有测试
2. **修改小功能**：比如改变线程池大小、添加日志
3. **添加新特性**：实现新的Awaiter、扩展数据结构
4. **性能调优**：使用工具分析瓶颈，优化性能

### 工具推荐
```bash
# 内存错误检测
valgrind --tool=memcheck ./flowcoro_tests

# 线程竞态检测  
valgrind --tool=helgrind ./flowcoro_tests

# 性能分析
perf record ./flowcoro_tests
perf report

# 汇编分析
objdump -d ./flowcoro_tests | less
```

## 📚 推荐阅读

### 书籍
1. **《C++20协程》** - Lewis Baker (在线资源)
2. **《The Art of Multiprocessor Programming》** - 无锁编程经典
3. **《C++ Concurrency in Action》** - C++并发编程权威指南

### 在线资源
1. [C++20协程官方提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4775.pdf)
2. [Lewis Baker的协程系列博客](https://lewissbaker.github.io/)
3. [folly库源码](https://github.com/facebook/folly) - 学习工业级实现

### 相关项目
1. **cppcoro** - C++协程库参考实现
2. **seastar** - 高性能C++框架
3. **liburing** - io_uring的C++封装

## 🎯 学习检验里程碑

### 里程碑1: 协程入门 (2周后)
- [ ] 能独立实现一个简单的生成器
- [ ] 理解co_await的执行流程
- [ ] 能解释Promise类型的作用

### 里程碑2: 无锁编程 (6周后)  
- [ ] 能实现一个线程安全的无锁计数器
- [ ] 理解不同内存序的应用场景
- [ ] 能分析并发算法的正确性

### 里程碑3: FlowCoro掌握 (8周后)
- [ ] 能独立扩展FlowCoro的功能
- [ ] 能优化现有代码的性能
- [ ] 能设计新的协程调度策略

## 💡 学习心得分享

学习这个项目的过程中，建议：

1. **不要急于求成**：协程和无锁编程都是复杂主题，需要时间消化
2. **多动手实践**：理论再多不如写代码实践
3. **善用调试工具**：gdb、valgrind是你的好朋友
4. **关注社区动态**：C++标准在快速演进，保持学习

记住：掌握这些技术后，你将具备开发高性能系统的能力！🚀
