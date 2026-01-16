# FlowCoro 

****

> :  [](PERFORMANCE_DATA.md)

## 

FlowCoro ****

- ****: lockfree::Queue
- ****: 
- **Task**: suspend_never

## 

```text
 (Task<T>)
    ↓ suspend_never
 (CoroutineManager) - 
    ↓ schedule_coroutine_enhanced
 (CoroutinePool) - 
    ↓ 
 (ThreadPool) - 
```

###  (CoroutineManager)

****: 

```cpp
class CoroutineManager {
public:
    //  - 
    void schedule_resume(std::coroutine_handle<> handle) {
        if (!handle || handle.done()) return;
        
        // 
        if (handle.promise().is_destroyed()) return;
        
        // 
        schedule_coroutine_enhanced(handle);
    }
    
    //  - 
    void drive() {
        drive_coroutine_pool();        // 
        process_timer_queue();         // (32/)
        process_ready_queue();         // (64/)
        process_pending_tasks();       // (32/)
    }
};
```

****:
- ****: 
- ****: 
- ****: 

###  (CoroutinePool)

****: 

```cpp
class CoroutinePool {
private:
    // CPU (4-16)
    const size_t NUM_SCHEDULERS;
    
    // 
    std::vector<std::unique_ptr<CoroutineScheduler>> schedulers_;
    
    // 
    SmartLoadBalancer load_balancer_;
    
public:
    void schedule_coroutine(std::coroutine_handle<> handle) {
        // 
        size_t scheduler_index = load_balancer_.select_scheduler();
        
        // 
        schedulers_[scheduler_index]->schedule_coroutine(handle);
        
        // 
        load_balancer_.increment_load(scheduler_index);
    }
};
```

****:
- ****: 4-16
- ****: lockfree::Queue
- ****: 

###  (ThreadPool)

****: 

```cpp
class ThreadPool {
private:
    // 
    lockfree::Queue<std::function<void()>> task_queue_;
    
    // 
    std::vector<std::thread> workers_;
    
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }
    
    void enqueue_void(std::function<void()> task) {
        if (!stop_.load(std::memory_order_acquire)) {
            task_queue_.enqueue(std::move(task));
        }
    }
    
private:
    void worker_loop() {
        std::function<void()> task;
        while (!stop_.load(std::memory_order_acquire)) {
            if (task_queue_.dequeue(task)) {
                task();  // 
            } else {
                std::this_thread::yield();
            }
        }
    }
};
```

****:
- ****: lockfree::Queue
- ****: CPU
- **CPU**: yieldspin-wait

## 

### 1. Task

```cpp
Task<int> compute(int x) {
    // Tasksuspend_never
    co_await sleep_for(std::chrono::milliseconds(50));
    co_return x * x;
}

// 
// 1. Task -> initial_suspend() -> suspend_never
// 2.  -> co_await -> suspend_always
// 3.  -> 
// 4.  ->  -> 
```

### 2. 

```text
Task task1 = compute(10);  // 
Task task2 = compute(20);  // 
Task task3 = compute(30);  // 

// 
auto result1 = co_await task1;  // 
auto result2 = co_await task2;  // 
auto result3 = co_await task3;  // 
```

### 3. 

```cpp
class SmartLoadBalancer {
    std::atomic<size_t> current_scheduler_{0};
    std::vector<std::atomic<size_t>> scheduler_loads_;
    
public:
    size_t select_scheduler() {
        //  + 
        size_t min_load = scheduler_loads_[0].load();
        size_t best_scheduler = 0;
        
        for (size_t i = 1; i < scheduler_loads_.size(); ++i) {
            size_t load = scheduler_loads_[i].load();
            if (load < min_load) {
                min_load = load;
                best_scheduler = i;
            }
        }
        
        return best_scheduler;
    }
};
```

## 

### 
- 
- 
- 

### suspend_never
- Task
- 
- 

### 
- 
- 
- 

### 
- Redis/Nginx
- malloc/free
- 

## 

****:
- ****: Web API 
- **-**: HTTP RPC 
- ****: 
- ****: 
- **-**: Channel

****:
- 
- 
- 

## 

 [](PERFORMANCE_DATA.md)


- ****: 
- ****: 
- ****: 
- ****: 
