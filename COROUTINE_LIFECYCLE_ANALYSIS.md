# FlowCoro 协程生命周期管理深度重构方案

## 📊 当前问题分析

### 1. 主要生命周期问题

#### 🔴 **协程句柄泄漏风险**
- **问题**: Task析构函数中直接调用 `handle.destroy()` 可能导致双重销毁
- **位置**: `core.h:215-225`, `core.h:332-342`, `core.h:695-705` 
- **风险**: 协程句柄状态检查不充分

#### 🔴 **异常处理与资源清理不一致**
- **问题**: 析构函数中异常处理过于简单，忽略清理失败
- **影响**: 可能导致资源泄漏或未定义行为

#### 🔴 **协程取消机制缺失**
- **问题**: 缺乏统一的协程取消和超时处理机制
- **对比**: cppcoro有完善的 `cancellation_token` 系统

#### 🔴 **协程状态追踪不完整**
- **问题**: 缺乏协程执行状态的统一管理
- **影响**: 难以调试和监控协程生命周期

### 2. 与业界标杆对比

#### ✅ **cppcoro的优势**
```cpp
// cppcoro的引用计数管理
class shared_task_promise_base {
    std::atomic<std::size_t> m_refCount{1};
    bool try_detach() noexcept {
        return m_refCount.fetch_sub(1, std::memory_order_acq_rel) != 1;
    }
};

// cppcoro的取消支持
class cancellation_registration {
    ~cancellation_registration();  // 自动清理
    void cancel() noexcept;
};
```

#### ✅ **Boost.Asio的优势**  
```cpp
// Asio的生命周期绑定
template<typename Executor>
class awaitable_thread {
    ~awaitable_thread() {
        if (bottom_of_stack_.valid()) {
            // 通过executor确保正确清理
            (post)(bottom_frame->u_.executor_, 
                [a = std::move(bottom_of_stack_)]() mutable {
                    // 安全销毁
                });
        }
    }
};
```

## 🛠️ 重构方案设计

### 阶段1: 协程句柄安全管理

#### 1.1 智能句柄包装器
```cpp
namespace flowcoro::detail {
    
class safe_coroutine_handle {
private:
    std::coroutine_handle<> handle_;
    std::atomic<bool> destroyed_{false};
    
public:
    safe_coroutine_handle() = default;
    
    explicit safe_coroutine_handle(std::coroutine_handle<> h) 
        : handle_(h) {}
    
    ~safe_coroutine_handle() {
        destroy();
    }
    
    // 不可拷贝，可移动
    safe_coroutine_handle(const safe_coroutine_handle&) = delete;
    safe_coroutine_handle& operator=(const safe_coroutine_handle&) = delete;
    
    safe_coroutine_handle(safe_coroutine_handle&& other) noexcept 
        : handle_(std::exchange(other.handle_, {}))
        , destroyed_(other.destroyed_.load()) {
        other.destroyed_.store(true);
    }
    
    safe_coroutine_handle& operator=(safe_coroutine_handle&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
            destroyed_.store(other.destroyed_.load());
            other.destroyed_.store(true);
        }
        return *this;
    }
    
    bool valid() const noexcept {
        return handle_ && !destroyed_.load(std::memory_order_acquire);
    }
    
    void resume() {
        if (valid() && !handle_.done()) {
            handle_.resume();
        }
    }
    
    bool done() const noexcept {
        return !handle_ || destroyed_.load(std::memory_order_acquire) || handle_.done();
    }
    
    void destroy() noexcept {
        bool expected = false;
        if (destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            if (handle_) {
                try {
                    handle_.destroy();
                } catch (...) {
                    // 记录但不重新抛出析构函数中的异常
                    LOG_ERROR("Exception during coroutine handle destruction");
                }
                handle_ = {};
            }
        }
    }
    
    auto address() const noexcept {
        return handle_ ? handle_.address() : nullptr;
    }
    
    template<typename Promise>
    Promise& promise() {
        return std::coroutine_handle<Promise>::from_address(handle_.address()).promise();
    }
};

}
```

#### 1.2 协程状态管理器
```cpp
namespace flowcoro::detail {

enum class coroutine_state {
    created,
    running,
    suspended,
    completed,
    destroyed,
    cancelled
};

class coroutine_state_manager {
private:
    std::atomic<coroutine_state> state_{coroutine_state::created};
    std::chrono::steady_clock::time_point creation_time_;
    std::optional<std::chrono::steady_clock::time_point> completion_time_;
    
public:
    coroutine_state_manager() : creation_time_(std::chrono::steady_clock::now()) {}
    
    coroutine_state get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    
    bool try_transition(coroutine_state from, coroutine_state to) noexcept {
        return state_.compare_exchange_strong(from, to, std::memory_order_acq_rel);
    }
    
    void force_transition(coroutine_state to) noexcept {
        if (to >= coroutine_state::completed && !completion_time_) {
            completion_time_ = std::chrono::steady_clock::now();
        }
        state_.store(to, std::memory_order_release);
    }
    
    auto get_lifetime() const {
        auto end = completion_time_.value_or(std::chrono::steady_clock::now());
        return end - creation_time_;
    }
    
    bool is_active() const noexcept {
        auto s = get_state();
        return s != coroutine_state::completed && 
               s != coroutine_state::destroyed && 
               s != coroutine_state::cancelled;
    }
};

}
```

### 阶段2: 取消和超时机制

#### 2.1 取消令牌系统
```cpp
namespace flowcoro {

class cancellation_token {
private:
    std::shared_ptr<detail::cancellation_state> state_;
    
public:
    cancellation_token() = default;
    explicit cancellation_token(std::shared_ptr<detail::cancellation_state> state)
        : state_(std::move(state)) {}
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw operation_cancelled_exception{};
        }
    }
    
    template<typename Callback>
    auto register_callback(Callback&& cb) {
        if (!state_) return cancellation_registration{};
        return state_->register_callback(std::forward<Callback>(cb));
    }
};

class cancellation_source {
private:
    std::shared_ptr<detail::cancellation_state> state_;
    
public:
    cancellation_source() 
        : state_(std::make_shared<detail::cancellation_state>()) {}
    
    cancellation_token get_token() {
        return cancellation_token{state_};
    }
    
    void cancel() noexcept {
        if (state_) {
            state_->request_cancellation();
        }
    }
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
};

}
```

#### 2.2 超时协程包装器
```cpp
namespace flowcoro {

template<typename T>
class timeout_task {
private:
    Task<T> task_;
    std::chrono::milliseconds timeout_;
    cancellation_source cancel_source_;
    
public:
    timeout_task(Task<T> task, std::chrono::milliseconds timeout)
        : task_(std::move(task)), timeout_(timeout) {}
    
    auto operator co_await() {
        struct awaiter {
            timeout_task& parent_;
            std::optional<T> result_;
            std::exception_ptr exception_;
            
            bool await_ready() { return false; }
            
            void await_suspend(std::coroutine_handle<> h) {
                auto token = parent_.cancel_source_.get_token();
                
                // 启动超时计时器
                auto timer_request = SleepManager::get().add_request(
                    std::coroutine_handle<>::from_address(nullptr), 
                    parent_.timeout_
                );
                
                // 启动主任务
                GlobalThreadPool::get().enqueue([
                    this, h, token, timer_request
                ]() mutable {
                    try {
                        if constexpr (std::is_void_v<T>) {
                            co_await parent_.task_;
                            
                            // 取消计时器
                            timer_request->cancelled.store(true);
                            
                            h.resume();
                        } else {
                            result_ = co_await parent_.task_;
                            
                            // 取消计时器  
                            timer_request->cancelled.store(true);
                            
                            h.resume();
                        }
                    } catch (...) {
                        timer_request->cancelled.store(true);
                        exception_ = std::current_exception();
                        h.resume();
                    }
                });
                
                // 启动超时检查
                GlobalThreadPool::get().enqueue_void([
                    this, h, timer_request, timeout = parent_.timeout_
                ]() {
                    std::this_thread::sleep_for(timeout);
                    
                    if (!timer_request->cancelled.load()) {
                        // 超时了，设置异常并恢复
                        exception_ = std::make_exception_ptr(
                            timeout_exception{"Operation timed out"}
                        );
                        parent_.cancel_source_.cancel();
                        h.resume();
                    }
                });
            }
            
            T await_resume() {
                if (exception_) {
                    std::rethrow_exception(exception_);
                }
                
                if constexpr (!std::is_void_v<T>) {
                    return std::move(result_.value());
                }
            }
        };
        
        return awaiter{*this};
    }
};

// 便利函数
template<typename T>
auto with_timeout(Task<T> task, std::chrono::milliseconds timeout) {
    return timeout_task<T>{std::move(task), timeout};
}

}
```

### 阶段3: 协程池和复用机制

#### 3.1 协程对象池
```cpp
namespace flowcoro::detail {

template<typename T>
class coroutine_pool {
private:
    lockfree::queue<std::unique_ptr<reusable_task_promise<T>>> available_;
    std::atomic<size_t> total_created_{0};
    std::atomic<size_t> in_use_{0};
    const size_t max_pool_size_;
    
public:
    explicit coroutine_pool(size_t max_size = 1000) 
        : max_pool_size_(max_size) {}
    
    ~coroutine_pool() {
        // 清理所有池中的协程
        std::unique_ptr<reusable_task_promise<T>> promise;
        while (available_.dequeue(promise)) {
            // promise会自动销毁
        }
    }
    
    auto acquire() -> std::unique_ptr<reusable_task_promise<T>> {
        std::unique_ptr<reusable_task_promise<T>> promise;
        
        if (available_.dequeue(promise)) {
            // 重用现有协程
            promise->reset();
            in_use_.fetch_add(1, std::memory_order_relaxed);
            return promise;
        }
        
        // 创建新协程
        if (total_created_.load(std::memory_order_acquire) < max_pool_size_) {
            promise = std::make_unique<reusable_task_promise<T>>();
            total_created_.fetch_add(1, std::memory_order_relaxed);
            in_use_.fetch_add(1, std::memory_order_relaxed);
            return promise;
        }
        
        // 池满了，返回nullptr表示需要等待或使用常规Task
        return nullptr;
    }
    
    void release(std::unique_ptr<reusable_task_promise<T>> promise) {
        if (promise && promise->is_reusable()) {
            in_use_.fetch_sub(1, std::memory_order_relaxed);
            available_.enqueue(std::move(promise));
        } else {
            // 协程不可重用或已损坏，直接销毁
            in_use_.fetch_sub(1, std::memory_order_relaxed);
            total_created_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    
    struct stats {
        size_t total_created;
        size_t available;
        size_t in_use;
        double reuse_rate;
    };
    
    stats get_stats() const {
        size_t created = total_created_.load();
        size_t used = in_use_.load();
        size_t avail = available_.size_approx();
        
        return stats{
            .total_created = created,
            .available = avail,
            .in_use = used,
            .reuse_rate = created > 0 ? double(avail + used) / created : 0.0
        };
    }
};

// 全局协程池管理器
template<typename T>
coroutine_pool<T>& get_coroutine_pool() {
    static coroutine_pool<T> pool;
    return pool;
}

}
```

### 阶段4: 重构后的Task实现

#### 4.1 新的Task设计
```cpp
namespace flowcoro {

template<typename T>
class Task {
private:
    detail::safe_coroutine_handle handle_;
    detail::coroutine_state_manager state_manager_;
    cancellation_token cancel_token_;
    
public:
    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr exception_;
        detail::coroutine_state_manager* state_manager_ = nullptr;
        cancellation_token cancel_token_;
        
        Task get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return Task{detail::safe_coroutine_handle{handle}};
        }
        
        std::suspend_never initial_suspend() noexcept { 
            if (state_manager_) {
                state_manager_->try_transition(
                    detail::coroutine_state::created,
                    detail::coroutine_state::running
                );
            }
            return {}; 
        }
        
        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type* promise_;
                
                bool await_ready() noexcept { return false; }
                
                void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                    if (promise_->state_manager_) {
                        promise_->state_manager_->force_transition(
                            detail::coroutine_state::completed
                        );
                    }
                }
                
                void await_resume() noexcept {}
            };
            
            return final_awaiter{this};
        }
        
        void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
            cancel_token_.throw_if_cancelled();
            value_ = std::move(value);
        }
        
        void unhandled_exception() { 
            exception_ = std::current_exception();
            if (state_manager_) {
                state_manager_->force_transition(detail::coroutine_state::destroyed);
            }
        }
        
        void set_cancellation_token(cancellation_token token) {
            cancel_token_ = std::move(token);
        }
    };
    
    explicit Task(detail::safe_coroutine_handle handle)
        : handle_(std::move(handle)) {
        if (handle_.valid()) {
            auto& promise = handle_.template promise<promise_type>();
            promise.state_manager_ = &state_manager_;
        }
    }
    
    // 移动语义
    Task(Task&& other) noexcept = default;
    Task& operator=(Task&& other) noexcept = default;
    
    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    ~Task() = default;  // safe_coroutine_handle会自动清理
    
    // 获取结果
    T get() {
        if (!handle_.valid()) {
            throw std::runtime_error("Invalid task handle");
        }
        
        // 等待协程完成
        while (handle_.valid() && !handle_.done()) {
            handle_.resume();
            
            // 检查取消状态
            cancel_token_.throw_if_cancelled();
            
            // 简单的让步，避免忙等
            std::this_thread::yield();
        }
        
        auto& promise = handle_.template promise<promise_type>();
        
        if (promise.exception_) {
            std::rethrow_exception(promise.exception_);
        }
        
        if (!promise.value_) {
            throw std::runtime_error("Task completed without value");
        }
        
        return std::move(promise.value_.value());
    }
    
    // 可等待接口
    struct awaiter {
        Task& task_;
        
        bool await_ready() {
            return task_.handle_.done();
        }
        
        void await_suspend(std::coroutine_handle<> waiting_handle) {
            if (task_.handle_.valid() && !task_.handle_.done()) {
                // 使用线程池异步执行
                GlobalThreadPool::get().enqueue_void([
                    handle = task_.handle_, 
                    waiting_handle,
                    token = task_.cancel_token_
                ]() mutable {
                    try {
                        // 检查取消状态
                        token.throw_if_cancelled();
                        
                        if (handle.valid() && !handle.done()) {
                            handle.resume();
                        }
                    } catch (...) {
                        // 异常会在await_resume中处理
                    }
                    
                    // 恢复等待的协程
                    if (waiting_handle && !waiting_handle.done()) {
                        waiting_handle.resume();
                    }
                });
            } else {
                // 任务已完成，直接恢复
                GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                    if (waiting_handle && !waiting_handle.done()) {
                        waiting_handle.resume();
                    }
                });
            }
        }
        
        T await_resume() {
            return task_.get();
        }
    };
    
    awaiter operator co_await() & {
        return awaiter{*this};
    }
    
    // 取消支持
    void cancel() {
        if (auto source = std::get_if<cancellation_source>(&cancel_token_)) {
            source->cancel();
        }
        state_manager_.force_transition(detail::coroutine_state::cancelled);
    }
    
    bool is_cancelled() const {
        return state_manager_.get_state() == detail::coroutine_state::cancelled ||
               cancel_token_.is_cancelled();
    }
    
    // 状态查询
    bool is_ready() const {
        return handle_.done();
    }
    
    detail::coroutine_state get_state() const {
        return state_manager_.get_state();
    }
    
    auto get_lifetime() const {
        return state_manager_.get_lifetime();
    }
};

}
```

## 📈 重构收益评估

### 1. 内存安全提升
- ✅ **句柄泄漏**: 通过 `safe_coroutine_handle` 完全消除
- ✅ **双重销毁**: 原子标志位防护
- ✅ **资源泄漏**: RAII确保清理

### 2. 功能完善度
- ✅ **取消支持**: 类似cppcoro的取消机制
- ✅ **超时处理**: 内置超时包装器
- ✅ **状态追踪**: 完整的生命周期管理

### 3. 性能优化
- ✅ **协程复用**: 对象池减少分配开销
- ✅ **无锁设计**: 关键路径无锁操作
- ✅ **内存局部性**: 相关数据聚合存储

### 4. 调试友好
- ✅ **状态可视化**: 详细的生命周期状态
- ✅ **统计信息**: 池使用率，重用率等指标
- ✅ **异常追踪**: 完整的异常传播链

## 🚀 实施路线图

### Phase 1: 基础设施 (Week 1-2)
1. 实现 `safe_coroutine_handle`
2. 实现 `coroutine_state_manager` 
3. 基础单元测试

### Phase 2: 取消机制 (Week 3-4)  
1. 实现 `cancellation_token` 系统
2. 实现 `timeout_task` 包装器
3. 集成测试

### Phase 3: 协程池 (Week 5-6)
1. 实现 `coroutine_pool`
2. 实现可重用协程promise
3. 性能基准测试

### Phase 4: 重构集成 (Week 7-8)
1. 重构现有Task实现
2. 迁移所有使用点
3. 完整测试套件

### Phase 5: 优化调优 (Week 9-10)
1. 性能调优
2. 内存使用优化
3. 文档更新

每个阶段都要求：
- ✅ 向后兼容的API设计
- ✅ 完整的单元测试覆盖
- ✅ 性能回归检查
- ✅ 内存泄漏检测

这个重构方案将显著提升FlowCoro的协程生命周期管理能力，使其达到业界领先水平。
