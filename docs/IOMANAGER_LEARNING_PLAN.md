# FlowCoro v2.2 学习ioManager特性实施方案

## 📋 项目概述

基于对ioManager项目的深入分析，我们计划在FlowCoro v2.2中引入其优秀设计理念，同时保持FlowCoro简洁易用的特点。

## 🎯 Phase 1: 性能优化 (v2.2 - 学习ioManager特性)

### 1.1 协程调度优化

#### 目标

保持协程切换性能优势（当前7ns vs ioManager的8.7ns），重点优化协程创建性能（当前181ns vs ioManager的208ns），并学习ioManager的状态管理机制。

#### 学习要点

- **FSM状态机模型**: ioManager的协程基于有限状态机，状态转换更清晰
- **协程生命周期管理**: 更精确的协程状态控制
- **调度器优化**: 减少不必要的内存分配和状态检查

#### 实施步骤

```cpp
// 1. 引入协程状态枚举
namespace flowcoro {
enum class CoroutineState {
    Created,    // 刚创建
    Running,    // 运行中  
    Suspended,  // 暂停
    Completed,  // 完成
    Cancelled   // 取消
};

// 2. 增强Task类的状态管理
template<typename T>
class Task {
private:
    CoroutineState state_ = CoroutineState::Created;
    std::chrono::steady_clock::time_point create_time_;
    
public:
    // 新增状态查询API
    CoroutineState get_state() const noexcept { return state_; }
    auto get_lifetime() const noexcept { 
        return std::chrono::steady_clock::now() - create_time_; 
    }
    bool is_active() const noexcept { 
        return state_ == CoroutineState::Running || state_ == CoroutineState::Suspended; 
    }
};
}
```

#### 性能指标
- 目标协程创建时间: <150ns (当前181ns, ioManager 208ns)
- 保持协程切换优势: 保持7ns水平 (ioManager 8.7ns)
- 内存使用优化: 减少10%协程内存占用

### 1.2 Future组合器实现

#### 目标
实现类似JavaScript Promise的all/any/race/allSettle操作，提升异步编程体验。

#### 学习要点
- **组合器模式**: ioManager提供的Future组合能力
- **错误传播**: 如何在组合器中正确处理错误
- **性能优化**: 避免不必要的协程创建

#### 实施步骤
```cpp
// 1. Future组合器基础框架
namespace flowcoro {

template<typename... Tasks>
class FutureAll {
private:
    std::tuple<Tasks...> tasks_;
    
public:
    explicit FutureAll(Tasks&&... tasks) : tasks_(std::forward<Tasks>(tasks)...) {}
    
    auto operator co_await() {
        return AllAwaiter{std::move(tasks_)};
    }
};

// 2. 便利函数
template<typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return FutureAll{std::forward<Tasks>(tasks)...};
}

template<typename... Tasks>
auto when_any(Tasks&&... tasks) {
    return FutureAny{std::forward<Tasks>(tasks)...};
}

template<typename... Tasks>
auto when_race(Tasks&&... tasks) {
    return FutureRace{std::forward<Tasks>(tasks)...};
}
}

// 3. 使用示例
Task<void> example_combinators() {
    auto task1 = fetch_data("url1");
    auto task2 = fetch_data("url2"); 
    auto task3 = fetch_data("url3");
    
    // 等待所有完成
    auto results = co_await when_all(std::move(task1), std::move(task2), std::move(task3));
    
    // 等待任意一个完成
    auto first_result = co_await when_any(fetch_data("url4"), fetch_data("url5"));
    
    co_return;
}
```

### 1.3 Channel通信机制

#### 目标
添加类似Golang的channel机制，支持协程间高效通信。

#### 学习要点
- **Channel设计**: ioManager的chan和async::chan区别
- **缓冲策略**: 无缓冲vs有缓冲channel
- **多生产者多消费者**: MPMC模式支持

#### 实施步骤
```cpp
// 1. Channel基础实现
namespace flowcoro {

template<typename T>
class Channel {
private:
    lockfree::Queue<T> queue_;
    std::atomic<bool> closed_{false};
    mutable std::mutex waiters_mutex_;
    std::vector<std::coroutine_handle<>> send_waiters_;
    std::vector<std::coroutine_handle<>> recv_waiters_;
    
public:
    explicit Channel(size_t capacity = 1024) : queue_(capacity) {}
    
    // 发送数据
    Task<void> send(const T& value);
    Task<void> send(T&& value);
    
    // 接收数据
    Task<std::optional<T>> receive();
    
    // 关闭channel
    void close();
    
    // 检查状态
    bool is_closed() const { return closed_.load(); }
};

// 2. 使用示例
Task<void> producer(Channel<int>& ch) {
    for (int i = 0; i < 10; ++i) {
        co_await ch.send(i);
        std::cout << "Sent: " << i << std::endl;
    }
    ch.close();
}

Task<void> consumer(Channel<int>& ch) {
    while (true) {
        auto value = co_await ch.receive();
        if (!value) break; // channel关闭
        std::cout << "Received: " << *value << std::endl;
    }
}
}
```

### 1.4 Pipeline流式处理

#### 目标
引入Pipeline概念，支持流式数据处理和协议链式组合。

#### 学习要点
- **协议抽象**: 输入协议和输出协议的抽象
- **适配器模式**: 不同协议间的数据转换
- **并行处理**: Pipeline段的独立并行执行

#### 实施步骤
```cpp
// 1. 协议接口定义
namespace flowcoro {

template<typename OutputType>
concept OutputProtocol = requires(T t, OutputType& out) {
    { t >> out } -> std::convertible_to<Task<void>>;
};

template<typename InputType>
concept InputProtocol = requires(T t, const InputType& in) {
    { t << in } -> std::convertible_to<Task<void>>;
};

// 2. Pipeline基础框架
template<typename... Protocols>
class Pipeline {
private:
    std::tuple<Protocols...> protocols_;
    
public:
    explicit Pipeline(Protocols&&... protocols) 
        : protocols_(std::forward<Protocols>(protocols)...) {}
    
    // 启动Pipeline
    Task<void> run();
    
    // 添加协议
    template<typename NewProtocol>
    auto operator>>(NewProtocol&& protocol) {
        return Pipeline<Protocols..., NewProtocol>{
            std::get<Protocols>(protocols_)..., 
            std::forward<NewProtocol>(protocol)
        };
    }
};

// 3. 便利函数
template<typename... Protocols>
auto make_pipeline(Protocols&&... protocols) {
    return Pipeline{std::forward<Protocols>(protocols)...};
}
}
```

## 📈 实施时间表

### Week 1-2: 协程调度优化
- [ ] 实现FSM状态机模型
- [ ] 优化协程创建和切换性能
- [ ] 添加状态查询API
- [ ] 性能基准测试

### Week 3-4: Future组合器
- [ ] 实现when_all组合器
- [ ] 实现when_any组合器  
- [ ] 实现when_race组合器
- [ ] 错误处理和测试

### Week 5-6: Channel通信
- [ ] 实现基础Channel类
- [ ] 添加发送接收操作
- [ ] 支持多生产者多消费者
- [ ] 性能优化和测试

### Week 7-8: Pipeline机制
- [ ] 设计协议抽象接口
- [ ] 实现Pipeline框架
- [ ] 添加适配器支持
- [ ] 集成测试和文档

## 🔧 技术挑战

### 1. 兼容性维护
- **挑战**: 新功能不能破坏现有API
- **方案**: 使用命名空间和条件编译，提供渐进式迁移

### 2. 性能保证
- **挑战**: 新功能不能影响现有性能
- **方案**: 零开销抽象，编译时优化，基准测试验证

### 3. 复杂度控制
- **挑战**: 学习ioManager同时保持简洁性
- **方案**: 分层设计，高级功能可选，简单接口优先

## 📊 成功指标

### 性能指标
- [ ] 协程创建性能提升5% (181ns -> 170ns)
- [ ] 协程切换性能保持在7ns以内
- [ ] Channel操作延迟<50ns
- [ ] Pipeline吞吐量>1M ops/sec

### 功能指标
- [ ] Future组合器API完整性100%
- [ ] Channel支持MPMC模式
- [ ] Pipeline支持至少5种协议
- [ ] 向后兼容性100%

### 质量指标
- [ ] 代码覆盖率>95%
- [ ] 性能回归测试通过率100%  
- [ ] 内存泄漏检测通过
- [ ] 文档完整性>90%

## 🧪 验证方案

### 1. 单元测试
```cpp
// Future组合器测试
TEST_CASE("when_all basic functionality") {
    auto task1 = []() -> Task<int> { co_return 1; }();
    auto task2 = []() -> Task<int> { co_return 2; }();
    
    auto results = when_all(std::move(task1), std::move(task2)).get();
    
    REQUIRE(std::get<0>(results) == 1);
    REQUIRE(std::get<1>(results) == 2);
}
```

### 2. 性能测试
```cpp
// 协程性能基准
BENCHMARK("coroutine_creation_v2") {
    return [](benchmark::State& state) {
        for (auto _ : state) {
            auto task = []() -> Task<void> { co_return; }();
            benchmark::DoNotOptimize(task);
        }
    };
}
```

### 3. 集成测试
- 与现有网络、数据库、RPC模块集成
- 真实场景压力测试
- 多线程环境稳定性测试

## 📚 文档计划

### 1. API文档更新
- Future组合器使用指南
- Channel编程模式
- Pipeline设计模式
- 迁移指南

### 2. 示例代码
- 生产者消费者模式演示
- 流式数据处理示例
- 错误处理最佳实践
- 性能优化技巧

### 3. 设计文档
- 架构设计决策记录
- 性能优化技术细节
- 与ioManager的差异对比
- 未来扩展计划

这个方案将帮助FlowCoro学习ioManager的优秀特性，同时保持自己的简洁性和实用性优势。
