#include <iostream>
#include <chrono>
#include <string>
#include <any>
#include <cmath>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace std::chrono;

// sleep_for
Task<int> fast_task() {
    // sleep_for
    std::cout << "...\n";
    co_await sleep_for(std::chrono::milliseconds(50));
    std::cout << "\n";
    co_return 42;
}

Task<std::string> slow_task() {
    // sleep_for
    std::cout << "...\n";
    co_await sleep_for(std::chrono::milliseconds(500));
    std::cout << "\n";
    co_return std::string("slow_result");
}

Task<double> medium_task() {
    // sleep_for
    std::cout << "...\n";
    co_await sleep_for(std::chrono::milliseconds(200));
    std::cout << "\n";
    co_return 3.14;
}

// when_any
TEST_CASE(when_any_basic) {
    std::cout << "\n=== when_any  ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto start = high_resolution_clock::now();
        
        // 
        auto result = co_await when_any(fast_task(), slow_task(), medium_task());
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << ": " << duration.count() << "ms\n";
        std::cout << ": " << result.first << "\n";
        
        // 0
        TEST_EXPECT_EQ(result.first, 0);
        
        // 
        if (result.first == 0) {
            auto value = std::any_cast<int>(result.second);
            std::cout << ": " << value << "\n";
            TEST_EXPECT_EQ(value, 42);
        }
        
        // 50ms
        TEST_EXPECT_TRUE(duration.count() < 150); // 
        
        std::cout << " when_any\n";
    }();
    
    sync_wait(std::move(test_coro));
}

// when_any
TEST_CASE(when_any_race) {
    std::cout << "\n=== when_any  ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto create_racing_task = [](int id, int computation_size) -> Task<int> {
            //  computation_size 
            volatile int sum = 0;
            for (int i = 0; i < computation_size; ++i) {
                sum += i;
            }
            std::cout << " " << id << "  (: " << computation_size << ")\n";
            co_return id;
        };
        
        // 
        auto start = high_resolution_clock::now();
        
        auto result = co_await when_any(
            create_racing_task(1, 50000),    // 
            create_racing_task(2, 100000),   // 
            create_racing_task(3, 150000),   // 
            create_racing_task(4, 200000)    // 
        );
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << ": " << duration.count() << "ms\n";
        std::cout << ":  " << std::any_cast<int>(result.second) << " (: " << result.first << ")\n";
        
        // 
        TEST_EXPECT_EQ(result.first, 0);
        TEST_EXPECT_EQ(std::any_cast<int>(result.second), 1);
        
        std::cout << " \n";
    }();
    
    sync_wait(std::move(test_coro));
}

// when_any
TEST_CASE(when_any_type_safety) {
    std::cout << "\n=== when_any  ===\n";
    
    auto test_coro = []() -> Task<void> {
        auto int_task = []() -> Task<int> {
            volatile int sum = 0;
            for (int i = 0; i < 30000; ++i) {
                sum += i;
            }
            co_return 123;
        };
        
        auto string_task = []() -> Task<std::string> {
            volatile int sum = 0;
            for (int i = 0; i < 100000; ++i) {
                sum += i;
            }
            co_return std::string("hello");
        };
        
        auto bool_task = []() -> Task<bool> {
            volatile int sum = 0;
            for (int i = 0; i < 150000; ++i) {
                sum += i;
            }
            co_return true;
        };
        
        auto result = co_await when_any(int_task(), string_task(), bool_task());
        
        std::cout << ": " << result.first << "\n";
        TEST_EXPECT_EQ(result.first, 0); // int_task
        
        // 
        if (result.first == 0) {
            auto value = std::any_cast<int>(result.second);
            std::cout << "int: " << value << "\n";
            TEST_EXPECT_EQ(value, 123);
        } else if (result.first == 1) {
            auto value = std::any_cast<std::string>(result.second);
            std::cout << "string: " << value << "\n";
            TEST_EXPECT_EQ(value, "hello");
        } else if (result.first == 2) {
            auto value = std::any_cast<bool>(result.second);
            std::cout << "bool: " << value << "\n";
            TEST_EXPECT_EQ(value, true);
        }
        
        std::cout << " \n";
    }();
    
    sync_wait(std::move(test_coro));
}

int main() {
    std::cout << "FlowCoro when_any ..." << std::endl;
    
    try {
        test_when_any_basic();
        test_when_any_race();
        test_when_any_type_safety();
    
        
        std::cout << "\nwhen_any\n";
        
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
