/**
 * @file advanced_features_demo.cpp
 * @brief FlowCoro
 */

#include <flowcoro.hpp>
#include <iostream>

using namespace flowcoro;

// 1yield
Task<int> cooperative_task(int id) {
    int result = 0;
    for (int i = 0; i < 1000; ++i) {
        result += i;
        
        // 100yield
        if (i % 100 == 0) {
            co_await yield();  // yield
        }
    }
    co_return result;
}

// 2yield
Task<void> batch_processing_task() {
    std::vector<int> data(10000);
    size_t counter = 0;
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = i * 2;
        
        // BatchYieldAwaiteryield
        co_await BatchYieldAwaiter(counter, 500);
    }
    
    std::cout << "Batch processing completed with " << counter << " yields\n";
}

// 3
Task<void> concurrent_workers() {
    std::vector<Task<int>> workers;
    
    // 
    for (int i = 0; i < 10; ++i) {
        workers.push_back(cooperative_task(i));
    }
    
    // 
    int total = 0;
    for (auto& worker : workers) {
        total += co_await worker;
    }
    
    std::cout << "Total result from " << workers.size() << " workers: " << total << "\n";
}

// 4voidsuspend_never
Task<void> immediate_void_task() {
    // Task<void>suspend_never
    std::cout << "Immediate void task executing!\n";
    co_return;
}

// 5
Task<void> combined_async_operations() {
    std::cout << "Starting combined operations...\n";
    
    // 1. 
    co_await immediate_void_task();
    
    // 2. yield
    co_await yield();
    
    // 3. 
    co_await sleep_for(std::chrono::milliseconds(10));
    
    // 4. 
    auto task1 = cooperative_task(1);
    auto task2 = cooperative_task(2);
    
    auto [value, index] = co_await when_any(std::move(task1), std::move(task2));
    std::cout << "First completed task (index " << index << ") returned: " << value << "\n";
    
    std::cout << "Combined operations completed!\n";
}

int main() {
    std::cout << "=== FlowCoro Advanced Features Demo ===\n\n";
    
    try {
        // 1: 
        std::cout << "Demo 1: Cooperative Task\n";
        auto result1 = sync_wait(cooperative_task(0));
        std::cout << "Result: " << result1 << "\n\n";
        
        // 2: 
        std::cout << "Demo 2: Batch Processing\n";
        sync_wait(batch_processing_task());
        std::cout << "\n";
        
        // 3: 
        std::cout << "Demo 3: Concurrent Workers\n";
        sync_wait(concurrent_workers());
        std::cout << "\n";
        
        // 4: void
        std::cout << "Demo 4: Immediate Void Task\n";
        sync_wait(immediate_void_task());
        std::cout << "\n";
        
        // 5: 
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
