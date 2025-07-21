#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <any>
#include <flowcoro.hpp>

using namespace flowcoro;

flowcoro::Task<int> async_compute(int value, int delay_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    co_return value;
}

flowcoro::Task<std::string> async_fetch(const std::string& data, int delay_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    co_return data + " processed";
}

flowcoro::Task<double> async_calculate(double value, int delay_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    co_return value * 2.0;
}

flowcoro::Task<void> async_void_task(int delay_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    co_return;
}

// Test when_any
flowcoro::Task<void> test_when_any() {
    std::cout << "=== Testing when_any ===" << std::endl;
    
    try {
        // Test with different types
        auto task1 = async_compute(42, 300);  // Will complete in 300ms
        auto task2 = async_fetch("hello", 100);  // Will complete first in 100ms  
        auto task3 = async_calculate(3.14, 200);  // Will complete in 200ms
        
        auto result = co_await when_any(std::move(task1), std::move(task2), std::move(task3));
        
        std::cout << "when_any completed with index: " << result.index << std::endl;
        
        // The first task (index 1, async_fetch) should complete first
        if (result.index == 1) {
            auto value = result.template get<std::string>();
            std::cout << "Result: " << value << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "when_any exception: " << e.what() << std::endl;
    }
    
    std::cout << std::endl;
}

// Test when_race  
flowcoro::Task<void> test_when_race() {
    std::cout << "=== Testing when_race ===" << std::endl;
    
    try {
        // Test with same types (race condition)
        auto task1 = async_compute(10, 150);
        auto task2 = async_compute(20, 100);  // Should win the race
        
        auto result = co_await when_race(std::move(task1), std::move(task2));
        
        std::cout << "when_race completed with index: " << result.index << std::endl;
        
        if (result.index == 0) {
            auto value = result.template get<int>();
            std::cout << "Result: " << value << std::endl;
        } else if (result.index == 1) {
            auto value = result.template get<int>();
            std::cout << "Result: " << value << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "when_race exception: " << e.what() << std::endl;
    }
    
    std::cout << std::endl;
}

// Test when_all_settled
flowcoro::Task<void> test_when_all_settled() {
    std::cout << "=== Testing when_all_settled ===" << std::endl;
    
    try {
        // Mix of tasks, some may succeed, some may fail
        auto task1 = async_compute(100, 50);
        auto task2 = async_compute(200, 100);  
        auto task3 = async_void_task(75);
        
        auto results = co_await when_all_settled(std::move(task1), std::move(task2), std::move(task3));
        
        std::cout << "when_all_settled completed with " << results.size() << " results" << std::endl;
        
        // Check each result
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "Task " << i << ": ";
            try {
                if (i < 2) {  // First two are int tasks
                    auto task_result = std::any_cast<flowcoro::TaskResult<int>>(results[i]);
                    if (task_result.success) {
                        std::cout << "Success, value: " << *task_result.value << std::endl;
                    } else {
                        std::cout << "Failed" << std::endl;
                    }
                } else {  // Third is void task
                    auto task_result = std::any_cast<flowcoro::TaskResult<void>>(results[i]);
                    if (task_result.success) {
                        std::cout << "Success (void)" << std::endl;
                    } else {
                        std::cout << "Failed" << std::endl;
                    }
                }
            } catch (const std::bad_any_cast& e) {
                std::cout << "Type cast error: " << e.what() << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cout << "when_all_settled exception: " << e.what() << std::endl;
    }
    
    std::cout << std::endl;
}

// Main test runner
flowcoro::Task<void> run_tests() {
    std::cout << "Starting Future Combinator Tests" << std::endl;
    std::cout << "=================================" << std::endl;
    
    co_await test_when_any();
    co_await test_when_race();
    co_await test_when_all_settled();
    
    std::cout << "All tests completed!" << std::endl;
}

int main() {
    try {
        run_tests().get();
    } catch (const std::exception& e) {
        std::cout << "Main exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
