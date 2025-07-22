# FlowCoro v2.3+ 核心健壮性与易用性升级规划

## 🎯 升级方向定位

基于v2.2.0已完成的Task生命周期管理增强，重点提升FlowCoro的**核心健壮性**和**开发易用性**，使其成为真正的生产级C++协程库。

## 🏗️ 当前技术基础评估

### ✅ 已有优势
- **Task生命周期管理**: v2.2.0已实现原子状态管理和安全销毁
- **Promise风格API**: 提供直观的状态查询接口  
- **基础协程调度**: 基于C++20标准协程的调度系统
- **无锁数据结构**: 高性能队列、栈等组件
- **异步网络IO**: 基于epoll的网络编程框架

### 🎯 待强化领域  
- **错误处理机制**: 缺乏统一的错误处理策略
- **资源管理**: 内存、文件描述符等资源的自动管理  
- **调试支持**: 缺乏协程调试和性能分析工具
- **API易用性**: 复杂操作的简化封装
- **文档和示例**: 需要更多实用示例和最佳实践

## 🚀 Phase 2.3: 核心健壮性增强 (优先级: 高)

### 1. 统一错误处理系统 
**目标**: 建立一致的错误处理模式，避免异常和错误码混用

#### 1.1 Result<T, E>类型系统
```cpp
// 类似Rust的Result类型
template<typename T, typename E = std::error_code>
class Result {
    std::variant<T, E> value_;
    
public:
    bool is_ok() const noexcept;
    bool is_err() const noexcept;
    T&& unwrap();                    // 获取值或抛出异常
    T unwrap_or(T default_value);    // 获取值或返回默认值
    
    // 链式操作
    template<typename F>
    auto and_then(F&& f) -> Result<...>;
    
    template<typename F>  
    auto map_err(F&& f) -> Result<T, ...>;
};
```

#### 1.2 协程错误传播
```cpp
// 自动错误传播的协程宏
#define CO_TRY(expr) \
    ({ auto result = (expr); \
       if (!result.is_ok()) co_return result.error(); \
       result.unwrap(); })

// 使用示例
Task<Result<std::string>> fetch_data(const std::string& url) {
    auto connection = CO_TRY(connect_to_server(url));
    auto response = CO_TRY(co_await send_request(connection));
    co_return Ok(std::move(response));
}
```

#### 1.3 错误类型体系
```cpp
enum class FlowCoroError {
    NetworkTimeout,
    DatabaseConnectionFailed,
    CoroutineDestroyed,
    ResourceExhausted,
    InvalidOperation
};

// 错误上下文
struct ErrorContext {
    std::string message;
    std::source_location location;
    std::vector<std::string> stack_trace;
};
```

### 2. RAII资源管理增强
**目标**: 自动化资源生命周期管理，防止资源泄漏

#### 2.1 资源守卫模板
```cpp
template<typename Resource, typename Deleter>
class ResourceGuard {
    Resource resource_;
    Deleter deleter_;
    bool released_ = false;
    
public:
    ResourceGuard(Resource r, Deleter d) : resource_(r), deleter_(d) {}
    ~ResourceGuard() { if (!released_) deleter_(resource_); }
    
    Resource get() const { return resource_; }
    Resource release() { released_ = true; return resource_; }
};

// 便利函数
template<typename R, typename D>
auto make_guard(R resource, D deleter) {
    return ResourceGuard<R, D>(resource, deleter);  
}
```

#### 2.2 协程作用域管理
```cpp
class CoroutineScope {
    std::vector<std::function<void()>> cleanup_tasks_;
    
public:
    template<typename F>
    void defer(F&& cleanup) {
        cleanup_tasks_.emplace_back(std::forward<F>(cleanup));
    }
    
    ~CoroutineScope() {
        for (auto& task : cleanup_tasks_ | std::views::reverse) {
            try { task(); } catch (...) {}
        }
    }
};

// 使用示例
Task<void> database_operation() {
    CoroutineScope scope;
    
    auto db = co_await connect_database();
    scope.defer([db]() { db.close(); });
    
    auto transaction = co_await db.begin_transaction();
    scope.defer([transaction]() { transaction.rollback(); });
    
    // 正常业务逻辑...
    co_await transaction.commit();
    // 自动清理资源
}
```

#### 2.3 内存池增强
```cpp
class SmartMemoryPool {
    std::unique_ptr<MemoryPool> pool_;
    std::atomic<size_t> allocated_count_{0};
    std::atomic<size_t> peak_usage_{0};
    
public:
    // 智能分配：根据使用模式自动调整策略
    template<typename T>
    std::unique_ptr<T, PoolDeleter<T>> allocate() {
        auto* ptr = pool_->allocate<T>();
        allocated_count_.fetch_add(1);
        
        return std::unique_ptr<T, PoolDeleter<T>>(
            ptr, 
            PoolDeleter<T>([this](T* p) {
                pool_->deallocate(p);
                allocated_count_.fetch_sub(1);
            })
        );
    }
    
    MemoryStats get_stats() const;
    void optimize_allocation_strategy();
};
```

### 3. 协程调度器优化
**目标**: 提升调度性能和公平性

#### 3.1 优先级调度
```cpp
enum class TaskPriority {
    Critical = 0,    // 系统关键任务
    High = 1,        // 用户交互任务  
    Normal = 2,      // 普通业务任务
    Low = 3,         // 后台任务
    Background = 4   // 清理任务
};

template<typename T>
class PriorityTask : public Task<T> {
    TaskPriority priority_;
    std::chrono::steady_clock::time_point deadline_;
    
public:
    void set_priority(TaskPriority p) { priority_ = p; }
    void set_deadline(std::chrono::milliseconds timeout) {
        deadline_ = std::chrono::steady_clock::now() + timeout;
    }
};
```

#### 3.2 负载均衡调度
```cpp
class LoadBalancedScheduler {
    struct WorkerStats {
        std::atomic<size_t> task_count{0};
        std::atomic<size_t> cpu_usage{0};
        std::chrono::steady_clock::time_point last_update;
    };
    
    std::vector<WorkerStats> worker_stats_;
    
    size_t select_best_worker() {
        // 基于负载和任务数量选择最优工作线程
        size_t best_worker = 0;
        size_t min_load = SIZE_MAX;
        
        for (size_t i = 0; i < worker_stats_.size(); ++i) {
            size_t load = calculate_load(i);
            if (load < min_load) {
                min_load = load;
                best_worker = i;
            }
        }
        return best_worker;
    }
};
```

### 4. 并发安全增强
**目标**: 消除数据竞争，提供更安全的并发原语

#### 4.1 原子引用计数指针
```cpp
template<typename T>
class AtomicSharedPtr {
    std::atomic<std::shared_ptr<T>*> ptr_;
    
public:
    std::shared_ptr<T> load() const {
        auto* raw_ptr = ptr_.load();
        return raw_ptr ? *raw_ptr : std::shared_ptr<T>{};
    }
    
    void store(std::shared_ptr<T> new_ptr) {
        auto* old_ptr = ptr_.exchange(new std::shared_ptr<T>(std::move(new_ptr)));
        delete old_ptr;
    }
    
    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired) {
        // 原子比较交换实现
    }
};
```

#### 4.2 读写锁协程化
```cpp
class CoroutineReadWriteLock {
    std::atomic<int> readers_{0};        // 负数表示写锁，正数表示读锁数量
    lockfree::Queue<std::coroutine_handle<>> waiting_writers_;
    lockfree::Queue<std::coroutine_handle<>> waiting_readers_;
    
public:
    Task<void> read_lock() {
        while (true) {
            int expected = readers_.load();
            if (expected >= 0) {  // 没有写锁
                if (readers_.compare_exchange_weak(expected, expected + 1)) {
                    co_return;  // 获取读锁成功
                }
            } else {
                // 需要等待
                co_await suspend_and_wait(waiting_readers_);
            }
        }
    }
    
    Task<void> write_lock() {
        int expected = 0;
        if (readers_.compare_exchange_strong(expected, -1)) {
            co_return;  // 获取写锁成功
        }
        co_await suspend_and_wait(waiting_writers_);
    }
};
```

## 🎨 Phase 2.4: 易用性大幅提升 (优先级: 高)

### 1. 链式API设计
**目标**: 提供流畅的API体验，减少样板代码

#### 1.1 任务构建器模式
```cpp
class TaskBuilder {
    std::string name_;
    TaskPriority priority_ = TaskPriority::Normal;
    std::chrono::milliseconds timeout_{0};
    std::function<void()> on_complete_;
    
public:
    TaskBuilder& named(std::string name) { 
        name_ = std::move(name); return *this; 
    }
    
    TaskBuilder& priority(TaskPriority p) { 
        priority_ = p; return *this; 
    }
    
    TaskBuilder& timeout(std::chrono::milliseconds t) { 
        timeout_ = t; return *this; 
    }
    
    TaskBuilder& on_complete(std::function<void()> callback) {
        on_complete_ = std::move(callback); return *this;
    }
    
    template<typename F>
    auto build(F&& func) -> Task<std::invoke_result_t<F>> {
        // 构建配置好的任务
    }
};

// 使用示例
auto task = TaskBuilder()
    .named("fetch_user_data")
    .priority(TaskPriority::High)
    .timeout(std::chrono::seconds(5))
    .on_complete([]() { log("Task completed"); })
    .build([]() -> Task<UserData> {
        // 任务逻辑
        co_return user_data;
    });
```

#### 1.2 管道操作符
```cpp
template<typename T>
class Pipeline {
    T value_;
    
public:
    template<typename F>
    auto operator|(F&& func) -> Pipeline<std::invoke_result_t<F, T>> {
        return Pipeline<std::invoke_result_t<F, T>>{func(std::move(value_))};
    }
    
    T unwrap() && { return std::move(value_); }
};

// 使用示例
auto result = Pipeline{fetch_data("user/123")}
    | validate_user_data
    | transform_to_json
    | save_to_cache
    | std::move.unwrap();
```

### 2. 协程组合器
**目标**: 提供高级协程组合操作

#### 2.1 安全的Future组合器（重新设计）
```cpp
// 重新设计，注重安全性
template<typename... Tasks>
Task<std::tuple<typename Tasks::value_type...>> when_all_safe(Tasks&&... tasks) {
    using ResultType = std::tuple<typename Tasks::value_type...>;
    
    // 使用RAII管理任务生命周期
    auto task_group = std::make_shared<TaskGroup<sizeof...(tasks)>>();
    
    // 安全地启动所有任务
    auto results = std::make_tuple(co_await std::forward<Tasks>(tasks)...);
    
    co_return results;
}

// 超时版本
template<typename... Tasks>
Task<Result<std::tuple<typename Tasks::value_type...>, TimeoutError>> 
when_all_timeout(std::chrono::milliseconds timeout, Tasks&&... tasks) {
    auto timer_task = sleep_for(timeout);
    auto all_task = when_all_safe(std::forward<Tasks>(tasks)...);
    
    auto result = co_await when_any(std::move(timer_task), std::move(all_task));
    
    if (result.index() == 0) {  // 超时
        co_return Err(TimeoutError{"Tasks timed out after " + std::to_string(timeout.count()) + "ms"});
    } else {  // 完成
        co_return Ok(std::get<1>(result));
    }
}
```

#### 2.2 重试机制
```cpp
template<typename F>
Task<Result<std::invoke_result_t<F>, RetryError>> 
retry_with_backoff(F&& func, int max_attempts = 3, std::chrono::milliseconds base_delay = std::chrono::milliseconds(100)) {
    
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            auto result = co_await func();
            co_return Ok(std::move(result));
            
        } catch (const std::exception& e) {
            if (attempt == max_attempts) {
                co_return Err(RetryError{
                    .attempts = attempt,
                    .last_error = e.what()
                });
            }
            
            // 指数退避
            auto delay = base_delay * (1 << (attempt - 1));
            co_await sleep_for(delay);
        }
    }
}

// 使用示例
auto result = co_await retry_with_backoff(
    []() -> Task<HttpResponse> {
        co_return co_await http_client.get("https://api.example.com/data");
    },
    /*max_attempts=*/5,
    /*base_delay=*/std::chrono::milliseconds(200)
);
```

### 3. 调试和诊断工具
**目标**: 提供强大的调试支持

#### 3.1 协程追踪器
```cpp
class CoroutineTracker {
    struct CoroutineInfo {
        std::string name;
        std::source_location creation_location;
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point last_resumed_at;
        size_t resume_count = 0;
        CoroutineState state;
    };
    
    std::unordered_map<void*, CoroutineInfo> active_coroutines_;
    std::mutex tracker_mutex_;
    
public:
    void register_coroutine(void* handle, std::string name, std::source_location loc = std::source_location::current());
    void on_resume(void* handle);
    void on_suspend(void* handle);
    void on_destroy(void* handle);
    
    std::vector<CoroutineInfo> get_active_coroutines() const;
    void dump_coroutine_state(std::ostream& os) const;
};
```

#### 3.2 性能分析器  
```cpp
class CoroutineProfiler {
    struct ProfileData {
        std::chrono::nanoseconds total_execution_time{0};
        std::chrono::nanoseconds total_suspend_time{0};
        size_t execution_count = 0;
        size_t suspend_count = 0;
    };
    
    std::unordered_map<std::string, ProfileData> profile_data_;
    
public:
    class ScopedTimer {
        CoroutineProfiler& profiler_;
        std::string task_name_;
        std::chrono::steady_clock::time_point start_time_;
        
    public:
        ScopedTimer(CoroutineProfiler& p, std::string name) 
            : profiler_(p), task_name_(std::move(name)), start_time_(std::chrono::steady_clock::now()) {}
        
        ~ScopedTimer() {
            auto duration = std::chrono::steady_clock::now() - start_time_;
            profiler_.record_execution(task_name_, duration);
        }
    };
    
    void record_execution(const std::string& name, std::chrono::nanoseconds duration);
    void generate_report(std::ostream& os) const;
};
```

## 🔧 Phase 2.5: 开发体验优化 (优先级: 中)

### 1. 配置系统
```cpp
class FlowCoroConfig {
    std::unordered_map<std::string, std::any> config_;
    
public:
    template<typename T>
    FlowCoroConfig& set(const std::string& key, T&& value) {
        config_[key] = std::forward<T>(value);
        return *this;
    }
    
    template<typename T>
    T get(const std::string& key, T default_value = {}) const {
        auto it = config_.find(key);
        if (it != config_.end()) {
            return std::any_cast<T>(it->second);
        }
        return default_value;
    }
};

// 全局配置
auto config = FlowCoroConfig()
    .set("thread_pool.size", 8)
    .set("memory_pool.initial_size", 1024 * 1024)
    .set("logging.level", LogLevel::INFO)
    .set("debugging.enable_profiling", true);
```

### 2. 一键式初始化
```cpp
class FlowCoroRuntime {
public:
    static FlowCoroRuntime& initialize(FlowCoroConfig config = {}) {
        static FlowCoroRuntime runtime(std::move(config));
        return runtime;
    }
    
    template<typename F>
    auto run(F&& main_coro) -> std::invoke_result_t<F> {
        setup_signal_handlers();
        start_background_services();
        
        try {
            return sync_wait(std::forward<F>(main_coro));
        } catch (...) {
            shutdown_gracefully();
            throw;
        }
    }
    
private:
    void setup_signal_handlers();
    void start_background_services();
    void shutdown_gracefully();
};

// 使用示例
int main() {
    auto& runtime = FlowCoroRuntime::initialize(
        FlowCoroConfig()
            .set("thread_pool.size", 4)
            .set("logging.level", LogLevel::DEBUG)
    );
    
    return runtime.run([]() -> Task<int> {
        // 主协程逻辑
        co_await do_work();
        co_return 0;
    });
}
```

## 📅 实施计划

### Sprint 1: 核心健壮性 (2-3周)
1. **错误处理系统** (1周)
   - 实现Result<T,E>类型
   - 协程错误传播机制
   - 错误类型体系设计

2. **RAII资源管理** (1周)  
   - ResourceGuard模板
   - CoroutineScope实现
   - 内存池增强

3. **并发安全** (0.5-1周)
   - 修复现有并发问题
   - 增强原子操作支持

### Sprint 2: 易用性提升 (2-3周)
1. **链式API** (1周)
   - TaskBuilder模式
   - Pipeline操作符

2. **协程组合器** (1-1.5周)  
   - 安全的when_all/when_any重新实现
   - 重试机制和超时处理

3. **调试工具** (0.5-1周)
   - 基础协程追踪
   - 简单性能分析

### Sprint 3: 开发体验 (1-2周)
1. **配置和初始化** (0.5-1周)
2. **文档和示例** (0.5-1周)
3. **测试和验证** (0.5周)

## 🎯 预期成果

完成后的FlowCoro v2.5将具备：

1. **工业级健壮性**
   - 统一的错误处理
   - 自动资源管理  
   - 线程安全保证

2. **极佳的开发体验**
   - 流畅的链式API
   - 强大的组合器
   - 完善的调试工具

3. **生产就绪**
   - 一键式部署
   - 完整的监控
   - 详细的文档

这个规划专注于**核心价值**，避免过早优化，确保每个特性都能显著提升开发者的使用体验。
