# Flowcoro 使用指南
## 最新更新 (v4.0)

### when_all 重大改进

- **优化的顺序执行**：新实现使用索引化顺序执行，避免了参数包展开的并发问题
- **内存优化**：单请求内存使用降至94-130 bytes/请求
- **极高性能**：支持10000+并发任务，吞吐量达71万+请求/秒
- **稳定性提升**：消除了跨线程协程恢复问题，彻底解决段错误问题

### ✅ sleep_for 协程友好设计

- **真正协程友好**：使用CoroutineManager的定时器系统，不阻塞线程
- **非阻塞实现**：通过 `await_suspend` 挂起协程，等待定时器恢复
- **单线程定时器**：定时器管理在单独线程中，避免复杂的多线程同步

## 快速开始

FlowCoro是一个现代的C++20协程库，提供简单易用的异步编程接口。**协程池是自动管理的，无需手动创建！**

## 最新更新 (v4.0)

### when_all 重大改进
- **真正的并发执行**：协程在多线程环境中并发执行，充分利用多核CPU
- **内存优化**：单请求内存使用降至94-130 bytes/请求
- **极高性能**：支持10000+并发任务，吞吐量达71万+请求/秒
- **稳定性提升**：多线程安全的协程调度，解决跨线程访问问题

### sleep_for 真正机制

- **协程友好实现**：使用定时器系统，不阻塞工作线程
- **真正的异步等待**：协程挂起后定时器负责恢复
- **线程安全设计**：定时器队列使用互斥锁保护，确保多线程安全

## 基本使用方式

### 并发机制概述

FlowCoro提供两种核心并发方式，满足不同业务场景：

#### 1. co_await 链式并发
- **执行方式**: 串行等待，内部并发执行
- **适用场景**: 有依赖关系的流水线处理
- **性能特点**: 启动延迟0.1μs，内存80字节/协程
- **使用时机**: 需要中间结果进行后续处理

```cpp
// co_await 链式并发示例
Task<void> pipeline_example() {
    // 1. 启动多个任务（立即并发开始）
    auto step1 = process_input();    // 任务1开始
    auto step2 = process_data();     // 任务2开始  
    auto step3 = process_output();   // 任务3开始
    
    // 2. 串行等待（获取中间结果）
    auto result1 = co_await step1;   // 等待任务1
    auto result2 = co_await step2;   // 任务2可能已完成
    auto result3 = co_await step3;   // 任务3可能已完成
    
    // 3. 可用中间结果进行后续处理
    auto final = co_await process_final(result1, result2, result3);
    co_return;
}
```

#### 2. when_all 批量并发
- **执行方式**: 显式批量并发等待
- **适用场景**: 独立任务批量处理
- **性能特点**: 大规模任务3.69M ops/sec
- **使用时机**: 不需要中间结果，追求最大并发度

```cpp
// when_all 批量并发示例
Task<void> batch_example() {
    auto task1 = async_compute(1);
    auto task2 = async_compute(2);
    auto task3 = async_compute(3);
    
    // 批量等待所有任务完成
    auto [r1, r2, r3] = co_await when_all(
        std::move(task1),
        std::move(task2),
        std::move(task3)
    );
    
    std::cout << "Results: " << r1 << ", " << r2 << ", " << r3 << std::endl;
    co_return;
}
```

#### 3. 混合并发模式
结合两种方式处理复杂业务逻辑：

```cpp
Task<void> hybrid_example() {
    // 阶段1：并发启动独立任务
    auto auth_task = authenticate();
    auto config_task = load_config();
    
    // 阶段2：等待关键任务，获取中间结果
    auto auth_result = co_await auth_task;
    
    // 阶段3：基于结果启动新任务
    auto data_task = load_data(auth_result.user_id);
    auto perm_task = load_permissions(auth_result.user_id);
    
    // 阶段4：批量等待剩余任务
    auto [config, data, perms] = co_await when_all(
        std::move(config_task),
        std::move(data_task),
        std::move(perm_task)
    );
    
    co_return;
}
```

### 1. 简单的协程任务

```cpp
#include "flowcoro.hpp"
using namespace flowcoro;

// 定义一个协程函数
Task<int> async_compute(int value) {
    std::cout << "开始计算: " << value << std::endl;

    // 协程友好的异步等待 - 真正机制：
    // 1. await_suspend() 将协程挂起，不阻塞工作线程
    // 2. 添加定时器到 CoroutineManager 的定时器队列
    // 3. 定时器到期后，drive() 方法恢复协程执行
    co_await sleep_for(std::chrono::milliseconds(100));

    int result = value * 2;
    std::cout << "计算完成: " << result << std::endl;

    co_return result;
}

int main() {
    // 方式1: 同步等待协程结果
    auto result = sync_wait(async_compute(21));
    std::cout << "结果: " << result << std::endl; // 输出: 42

    return 0;
}
```

### 2. 并发机制实践

基于前面的概述，以下是具体的实践示例：

#### A. co_await 链式并发实践

```cpp
// 流水线处理示例：每步都需要前一步的结果
Task<std::string> pipeline_processing(const std::string& input) {
    // 阶段1：输入验证
    auto validation_task = validate_input(input);
    auto validation_result = co_await validation_task;
    
    if (!validation_result.valid) {
        co_return "输入无效";
    }
    
    // 阶段2：数据处理（基于验证结果）
    auto processing_task = process_data(validation_result.cleaned_data);
    auto processing_result = co_await processing_task;
    
    // 阶段3：结果格式化（基于处理结果）
    auto formatting_task = format_result(processing_result);
    auto final_result = co_await formatting_task;
    
    co_return final_result;
}
```

#### B. when_all 批量并发实践

#### B. when_all 批量并发实践

**固定数量任务批处理：**

```cpp
// 定义不同类型的异步任务
Task<int> compute_task(int value) {
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return value * 2;
}

Task<std::string> fetch_data(const std::string& key) {
    // 协程友好的异步等待 - 真正机制：
    // 1. await_suspend() 将协程挂起，不阻塞工作线程
    // 2. 添加定时器到 CoroutineManager 的定时器队列
    // 3. 定时器到期后，drive() 方法恢复协程执行
    co_await sleep_for(std::chrono::milliseconds(150));
    co_return "数据:" + key;
}

Task<void> when_all_fixed_tasks() {
    // 创建不同类型的独立任务
    auto task1 = compute_task(21);
    auto task2 = compute_task(42);
    auto task3 = fetch_data("user123");

    // when_all：所有任务并发执行，一次性等待完成
    auto [result1, result2, result3] = co_await when_all(
        std::move(task1),
        std::move(task2),
        std::move(task3)
    );

    std::cout << "计算结果1: " << result1 << std::endl; // 42
    std::cout << "计算结果2: " << result2 << std::endl; // 84
    std::cout << "获取数据: " << result3 << std::endl;   // "数据:user123"
    co_return;
}
```

**动态数量任务处理：**

```cpp
Task<void> when_all_dynamic_tasks(int task_count) {
    // 创建动态数量的同类型任务
    std::vector<Task<std::string>> tasks;
    tasks.reserve(task_count);

    for (int i = 0; i < task_count; ++i) {
        tasks.emplace_back([](int id) -> Task<std::string> {
            co_await sleep_for(std::chrono::milliseconds(50));
            co_return "任务" + std::to_string(id) + "完成";
        }(1000 + i));
    }

    // 方式1：使用when_all_vector（推荐大批量任务）
    auto results = co_await when_all_vector(std::move(tasks));
    
    std::cout << "并发完成 " << results.size() << " 个任务" << std::endl;
    
    // 方式2：逐个co_await（内部仍然并发）
    // 注意：即使是循环co_await，底层协程仍在多线程中并发执行
    co_return;
}
```

#### C. 混合并发模式实践

```cpp
Task<UserSession> complex_user_login(const std::string& username, const std::string& password) {
    // 第1阶段：并发启动多个独立的预处理任务
    auto auth_task = authenticate_user(username, password);
    auto rate_limit_task = check_rate_limit(username);
    auto system_status_task = check_system_status();
    
    // 第2阶段：等待关键任务，获取认证结果
    auto auth_result = co_await auth_task;
    if (!auth_result.success) {
        co_return UserSession{}; // 认证失败，提前返回
    }
    
    // 第3阶段：基于认证结果，启动用户相关任务
    auto user_data_task = load_user_data(auth_result.user_id);
    auto permissions_task = load_user_permissions(auth_result.user_id);
    auto preferences_task = load_user_preferences(auth_result.user_id);
    
    // 第4阶段：when_all等待所有剩余任务
    auto [rate_limit, system_status, user_data, permissions, preferences] = co_await when_all(
        std::move(rate_limit_task),
        std::move(system_status_task),
        std::move(user_data_task),
        std::move(permissions_task),
        std::move(preferences_task)
    );
    
    // 第5阶段：构建完整的用户会话
    UserSession session;
    session.user_id = auth_result.user_id;
    session.user_data = user_data;
    session.permissions = permissions;
    session.preferences = preferences;
    session.rate_limit_info = rate_limit;
    session.system_status = system_status;
    
    co_return session;
}
```

### 3. 性能最佳实践

#### 并发模式选择指南

| 场景类型 | 推荐模式 | 原因 |
|----------|----------|------|
| **有依赖的流水线** | co_await链式 | 需要中间结果，内存效率高 |
| **独立任务批处理** | when_all批量 | 最大并发度，吞吐量最高 |
| **复杂业务逻辑** | 混合模式 | 兼具两者优势，灵活性最好 |
| **超大规模任务** | when_all_vector | 支持10000+任务，线性扩展 |

#### 内存和性能优化

```cpp
// ✅ 推荐：预分配容器，避免co_await后的内存分配
Task<std::vector<std::string>> optimized_batch_processing(int count) {
    std::vector<Task<std::string>> tasks;
    tasks.reserve(count); // 预分配，避免动态扩展
    
    std::vector<std::string> results;
    results.reserve(count); // 预分配结果容器
    
    // 批量创建任务
    for (int i = 0; i < count; ++i) {
        tasks.emplace_back(async_string_task(i));
    }
    
    // 批量执行
    auto batch_results = co_await when_all_vector(std::move(tasks));
    
    co_return batch_results;
}

// ❌ 不推荐：co_await后进行内存分配
Task<void> inefficient_processing() {
    auto result = co_await some_task();
    
    // 不推荐：co_await后的动态内存分配可能影响性能
    std::vector<std::string> dynamic_vector;
    for (int i = 0; i < result.size(); ++i) {
        dynamic_vector.push_back("item" + std::to_string(i));
    }
}
```

### 3. 手动控制协程池（高级用法）

如果需要更精细的控制，可以手动驱动协程池：

```cpp
int main() {
    auto task = async_compute(42);

    // 手动提交协程到协程池
    if (task.handle) {
        schedule_coroutine_enhanced(task.handle);
    }

    // 手动驱动事件循环
    while (!task.handle.done()) {
        drive_coroutine_pool(); // 驱动协程池
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
```

## 核心API说明

### 协程相关

- **`Task<T>`**: 协程任务类型，T是返回值类型
- **`co_await`**: 等待另一个协程完成
- **`co_return`**: 从协程返回值
- **`sleep_for(duration)`**: 协程友好的异步等待，使用定时器系统实现
- **`when_all(tasks...)`**: 并发等待多个固定任务完成，返回所有结果的元组

#### sleep_for 实现机制

```cpp
// sleep_for 的真正工作方式
Task<void> timing_example() {
    std::cout << "开始等待..." << std::endl;
    
    // 这不会阻塞工作线程！实际发生的事情：
    // 1. await_suspend() 挂起当前协程
    // 2. 添加定时器到 CoroutineManager 的定时器队列  
    // 3. 当前工作线程继续处理其他协程
    // 4. 定时器线程在指定时间后恢复这个协程
    co_await sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "等待完成！" << std::endl;
    co_return;
}
```

### 执行控制

- **`sync_wait(task)`**: **同步阻塞**等待协程完成，会暂停当前线程执行
- **`co_await task`**: **异步非阻塞**等待协程完成，允许协程调度器继续工作
- **`schedule_coroutine_enhanced(handle)`**: 手动将协程提交到协程池
- **`drive_coroutine_pool()`**: 手动驱动协程池执行

#### sync_wait vs co_await 使用指南

```cpp
// ✅ 正确：在main函数中使用sync_wait
int main() {
    flowcoro::enable_v2_features();
    auto result = sync_wait(my_async_task()); // 阻塞主线程直到完成
    return 0;
}

// ❌ 错误：在协程中使用sync_wait
Task<void> wrong_way() {
    auto result = sync_wait(other_task()); // 阻塞整个协程调度器！
    co_return;
}

// ✅ 正确：在协程中使用co_await
Task<void> right_way() {
    auto result = co_await other_task(); // 异步等待，不阻塞调度器
    co_return;
}
```

### 重要说明：真正的并发执行

**FlowCoro 内部使用多线程协程调度，即使看起来是"顺序"的代码实际上也是并发执行的：**

```cpp
// 这看起来是顺序执行
for (auto& task : tasks) {
    auto result = co_await task;  // 每个协程都在不同线程中并发执行
    results.push_back(result);
}
```

- **内部机制**: 每个 `co_await` 都会将协程提交到线程池
- **并发执行**: 多个协程同时在32-128个工作线程中执行
- **异步等待**: `co_await` 不会阻塞线程，而是挂起当前协程
- **性能表现**: 即使是循环也能达到70万+请求/秒的吞吐量

### 协程池管理（自动）

- 协程池在首次使用时**自动启动**
- 默认使用单线程事件循环 + 多线程工作池
- 无需手动创建或管理协程池

## 关键特性

### 自动协程池管理

- **无需手动创建**: 协程池自动初始化和管理
- **多线程调度**: 32-128个工作线程并发执行协程
- **线程安全**: SafeCoroutineHandle确保跨线程访问安全

### 性能优势

- **轻量级**: 协程创建开销极低（微秒级）
- **真正并发**: 支持大规模并发（万级协程在多线程中执行）
- **内存高效**: 相比线程节省640倍内存

### 安全保障

- **多线程安全**: 原子操作和互斥锁确保线程安全
- **异常处理**: 完善的错误处理机制
- **生命周期管理**: 自动管理协程生命周期

## 使用建议

### 推荐用法

1. **主函数场景**: 使用 `sync_wait()` 等待最终结果
2. **协程内部**: 使用 `co_await` 等待其他协程（异步非阻塞）
3. **网络IO**: 使用异步等待避免阻塞整个调度器
4. **定时任务**: 使用 `sleep_for()` 实现协程友好的定时器

### 避免的用法

1. **❌ 在协程中使用sync_wait**: 会阻塞整个协程调度器
2. **❌ 在协程外使用co_await**: 只能在协程函数内使用
3. **❌ 忘记co_return**: 协程函数必须有返回语句
4. **❌ 手动管理协程池**: 除非有特殊性能需求
5. **️ when_all 使用限制**:
   - 不能直接用于 `std::vector<Task<T>>`（动态数量任务）
   - 适合 2-10 个固定数量任务
   - 超过 10 个任务建议分批处理

```cpp
// 错误 - 协程恢复后字符串分配可能失败
Task<std::string> bad_task(const std::string& name) {
    co_await sleep_for(std::chrono::milliseconds(100));
    return "结果:" + name; // 可能触发 std::bad_alloc
}

// 错误 - when_all 不支持动态数量任务
std::vector<Task<int>> tasks = create_many_tasks(100);
// auto results = co_await when_all(tasks); // 编译错误！

// 正确 - 在协程挂起前预构建字符串
Task<std::string> good_task(const std::string& name) {
    std::string result = "结果:" + name; // 预构建
    co_await sleep_for(std::chrono::milliseconds(100));
    co_return result;
}

// 正确 - 动态数量任务使用循环或when_all风格管理
std::vector<Task<int>> tasks = create_many_tasks(100);
std::vector<int> results;
for (auto& task : tasks) {
    results.push_back(co_await task); // 或使用协程池管理
}

// 正确 - when_all 用于固定数量任务
auto [r1, r2, r3] = co_await when_all(task1, task2, task3);
```

## 性能对比

基于实测数据（10000个并发任务）：

| 方案 | 执行时间 | 内存使用 | 并发能力 | 吞吐量 | 线程利用 |
|------|----------|----------|----------|--------|----------|
| FlowCoro协程 | 13ms | 792kb | 万级 | 769230 请求/秒 | 32-128线程 |
| 传统多线程 | 778ms | 2.86MB | 千级 | 12853 请求/秒 | 10000线程 |
| **性能提升** | **60倍** | **4倍** | **60倍** | **70倍** | **99%节省** |

### 并发执行的优势

- **真正的并发**: 协程在多线程环境中并发执行，不是伪并发
- **线程复用**: 32-128个工作线程处理万级协程任务
- **智能调度**: 自动负载均衡，最大化CPU利用率
- **低延迟**: 协程切换成本远低于线程上下文切换

## 完整示例

参考项目中的以下文件：

- `examples/hello_world_concurrent.cpp` - 并发处理示例
- `tests/test_core.cpp` - 单元测试示例

## 进阶功能

- **网络IO**: HTTP客户端、RPC支持
- **数据库**: 异步数据库连接池
- **错误处理**: Result<T,E> 错误处理机制
- **日志系统**: 高性能异步日志
- **内存管理**: 内存池、对象池优化
