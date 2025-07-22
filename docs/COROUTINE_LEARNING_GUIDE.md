# C++20 协程学习指南

## 协程基础概念

C++20协程是一种特殊的函数，具有挂起和恢复的能力。协程是**无栈（stackless）**协程，相比传统的有栈协程（如Linux的Context Switch）更加轻量。

## 核心特点

1. **无栈协程**：不需要保存整个调用栈，只保存必要的状态
2. **非对称协程**：协程之间的切换不是对称的，类似Windows的纤程
3. **异步编程**：主要用于异步IO操作，避免阻塞线程

## 三个关键字

### co_await
- 用法：`result = co_await awaiter;`
- 功能：调用awaiter对象，根据其接口决定是否挂起
- 返回值：awaiter的await_resume()接口返回值

### co_yield  
- 用法：`co_yield value;`
- 功能：挂起协程并返回值给调用者
- 相当于：`co_await promise.yield_value(value)`

### co_return
- 用法：`co_return value;`
- 功能：协程返回并结束
- 调用：`promise.return_value(value)`或`promise.return_void()`

## 重要概念

### 协程状态 (Coroutine State)
- 分配在堆上的内部对象
- 包含：承诺对象、函数参数、局部变量、挂起点信息
- 注意：协程状态 ≠ 协程返回值对象

### 承诺对象 (Promise)  
```cpp
struct promise_type {
    auto get_return_object();     // 构造协程返回值
    auto initial_suspend();       // 初始化后行为
    void return_value(T v);       // 处理co_return
    auto yield_value(T v);        // 处理co_yield  
    auto final_suspend() noexcept; // 协程结束时行为
    void unhandled_exception();   // 异常处理
};
```

### 协程句柄 (Coroutine Handle)
```cpp
std::coroutine_handle<promise_type> handle;
handle.resume();  // 恢复协程
handle.done();    // 检查是否完成
handle.destroy(); // 销毁协程
```

### 等待体 (Awaiter)
```cpp
struct awaiter {
    bool await_ready();                    // 是否准备好
    void await_suspend(coroutine_handle<> h); // 挂起时操作
    auto await_resume();                   // 恢复时返回值
};
```

## 协程执行流程

1. **创建阶段**：
   - 分配协程状态 (coroutine state)
   - 构造承诺对象 (promise)
   - 调用 `promise.get_return_object()` 构造返回值
   - 调用 `co_await promise.initial_suspend()`

2. **co_await 执行**：
   - 计算awaiter对象
   - 调用 `awaiter.await_ready()`
   - 如果未准备好，调用 `awaiter.await_suspend(handle)`
   - 协程挂起，等待外部恢复
   - 恢复时调用 `awaiter.await_resume()`

3. **co_return 执行**：
   - 调用 `promise.return_value(value)`
   - 调用 `co_await promise.final_suspend()`
   - 根据final_suspend返回值决定是否自动销毁

## 异步IO设计模式

### 传统AIO模式
```cpp
struct await_read_file {
    bool await_ready() { return false; }
    
    void await_suspend(std::coroutine_handle<> awaiting) {
        // 1. 设置回调函数
        callback = [awaiting](result) {
            // 2. 在IO完成后恢复协程
            awaiting.resume();
        };
        // 3. 提交异步请求到线程池
        thread_pool.submit(io_request);
    }
    
    auto await_resume() { return result; }
};
```

### 消息队列模式
1. 请求队列：主线程提交IO请求
2. 工作线程：执行实际IO操作
3. 响应队列：返回完成结果
4. 主循环：检查响应队列，调用回调函数

## 协程vs传统异步

### 传统C++异步问题
- 每个异步操作可能启动新线程（开销大）
- future.get()需要阻塞等待
- 难以管理多个异步操作的生命周期

### 协程优势
- 轻量级：无栈协程内存开销小
- 非阻塞：co_await不阻塞线程
- 结构化：代码看起来像同步，实际异步执行
- 可组合：多个协程可以方便组合

## 最佳实践

### 协程设计原则
1. **避免多线程陷阱**：不要在不同线程间直接恢复协程
2. **统一事件循环**：所有协程恢复应在同一线程
3. **RAII管理**：使用智能指针管理协程生命周期
4. **错误处理**：通过Result<T,E>统一错误处理

### 内存管理
```cpp
struct promise_type {
    // 可以重载operator new/delete使用自定义内存分配
    void* operator new(std::size_t size) {
        return custom_allocator.allocate(size);
    }
    
    void operator delete(void* ptr, std::size_t size) {
        custom_allocator.deallocate(ptr, size);
    }
};
```

## 实际应用场景

1. **网络IO**：HTTP请求、TCP连接
2. **文件IO**：异步文件读写
3. **数据库**：异步数据库查询
4. **定时器**：异步等待
5. **消息队列**：生产者消费者模式

## 注意事项

1. **生命周期管理**：协程可能比创建它的对象活得更久
2. **异常安全**：unhandled_exception必须正确实现
3. **final_suspend**：返回suspend_always需要手动destroy()
4. **线程安全**：协程状态不是线程安全的

这个学习总结帮助我们理解了C++20协程的核心概念。基于这些知识，我们可以更好地设计FlowCoro的协程系统。
