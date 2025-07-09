# FlowCoro

一个基于C++20 coroutine的高性能协程库，适合大厂面试和真实项目原型。

## 特性
- C++20 coroutine语法支持
- 线程池（ThreadPool）
- 内存池（MemoryPool）
- 对象池（ObjectPool）
- 简单易用的Task/CoroTask
- 适合高并发、异步、资源复用场景

## 示例
```cpp
#include "flowcoro.h"
#include "thread_pool.h"
#include <iostream>

flowcoro::CoroTask simple_coro() {
    std::cout << "Hello from coroutine!" << std::endl;
    co_return;
}

flowcoro::Task<int> add(int a, int b) {
    co_return a + b;
}

int main() {
    auto co = simple_coro();
    co.resume();

    auto t = add(3, 4);
    std::cout << "add(3,4) = " << t.get() << std::endl;
    return 0;
}
```

## 适用场景
- 高并发网络服务
- 任务调度与资源池化
- 面试算法与系统设计题原型

## 扩展
如需更多高级功能（如定时器、协程间通信、awaitable IO等），可在此基础上扩展。
