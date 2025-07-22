#include <iostream>
#include <chrono>
#include <thread>
#include <flowcoro.hpp>

using namespace flowcoro;

flowcoro::Task<int> simple_async_task(int value) {
    std::cout << "Task executing with value " << value << std::endl;
    co_return value;
}

// Test without when_any first
flowcoro::Task<void> test_basic_coroutine() {
    std::cout << "=== Basic Coroutine Test ===" << std::endl;
    
    try {
        auto task1 = simple_async_task(42);
        auto result = co_await task1;
        std::cout << "Got result: " << result << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
    }
    
    std::cout << "Basic coroutine test finished." << std::endl;
}

int main() {
    try {
        std::cout << "Starting basic coroutine test..." << std::endl;
        test_basic_coroutine().get();
        std::cout << "Test completed successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Main exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
