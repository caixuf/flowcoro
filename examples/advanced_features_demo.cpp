/**
 * @file advanced_features_demo.cpp
 * @brief 演示FlowCoro的高级架构特性
 */

#include <flowcoro.hpp>
#include <iostream>

using namespace flowcoro;

// 演示1：使用yield进行协作式调度
Task<int> cooperative_task(int id) {
    int result = 0;
    for (int i = 0; i < 1000; ++i) {
        result += i;
        
        // 每100次迭代yield一次，让其他协程有机会执行
        if (i % 100 == 0) {
            co_await yield();  // 轻量级yield，不需要完整的重新调度
        }
    }
    co_return result;
}

// 演示2：批量处理与周期性yield
Task<void> batch_processing_task() {
    std::vector<int> data(10000);
    size_t counter = 0;
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = i * 2;
        
        // 使用BatchYieldAwaiter自动管理yield频率
        co_await BatchYieldAwaiter(counter, 500);
    }
    
    std::cout << "Batch processing completed with " << counter << " yields\n";
}

// 演示3：高性能并发任务
Task<void> concurrent_workers() {
    std::vector<Task<int>> workers;
    
    // 创建多个协作式工作任务
    for (int i = 0; i < 10; ++i) {
        workers.push_back(cooperative_task(i));
    }
    
    // 等待所有任务完成
    int total = 0;
    for (auto& worker : workers) {
        total += co_await worker;
    }
    
    std::cout << "Total result from " << workers.size() << " workers: " << total << "\n";
}

// 演示4：同步启动的void任务（利用suspend_never）
Task<void> immediate_void_task() {
    // 这个任务会同步开始执行，因为Task<void>现在使用suspend_never
    std::cout << "Immediate void task executing!\n";
    co_return;
}

// 演示5：组合使用不同的异步原语
Task<void> combined_async_operations() {
    std::cout << "Starting combined operations...\n";
    
    // 1. 同步启动的任务
    co_await immediate_void_task();
    
    // 2. 协作式yield
    co_await yield();
    
    // 3. 延时操作
    co_await sleep_for(std::chrono::milliseconds(10));
    
    // 4. 并发任务
    auto task1 = cooperative_task(1);
    auto task2 = cooperative_task(2);
    
    auto [value, index] = co_await when_any(std::move(task1), std::move(task2));
    std::cout << "First completed task (index " << index << ") returned: " << value << "\n";
    
    std::cout << "Combined operations completed!\n";
}

int main() {
    std::cout << "=== FlowCoro Advanced Features Demo ===\n\n";
    
    try {
        // 演示1: 协作式任务
        std::cout << "Demo 1: Cooperative Task\n";
        auto result1 = sync_wait(cooperative_task(0));
        std::cout << "Result: " << result1 << "\n\n";
        
        // 演示2: 批量处理
        std::cout << "Demo 2: Batch Processing\n";
        sync_wait(batch_processing_task());
        std::cout << "\n";
        
        // 演示3: 并发工作器
        std::cout << "Demo 3: Concurrent Workers\n";
        sync_wait(concurrent_workers());
        std::cout << "\n";
        
        // 演示4: 同步启动的void任务
        std::cout << "Demo 4: Immediate Void Task\n";
        sync_wait(immediate_void_task());
        std::cout << "\n";
        
        // 演示5: 组合操作
        std::cout << "Demo 5: Combined Async Operations\n";
        sync_wait(combined_async_operations());
        std::cout << "\n";
        
        std::cout << "=== All demos completed successfully! ===\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
