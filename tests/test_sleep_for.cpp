#include "flowcoro.hpp"
#include "test_framework.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace flowcoro;

//  sleep_for 
TEST_CASE(basic_sleep_for) {
    std::cout << " sleep_for ..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << "" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(100));
        std::cout << "100ms" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << ": " << elapsed.count() << "ms" << std::endl;
    
    //  (±50ms)
    TEST_EXPECT_TRUE(elapsed.count() >= 50 && elapsed.count() <= 200);
    
    std::cout << " sleep_for !" << std::endl;
}

TEST_CASE(sequential_sleep_for) {
    std::cout << " sleep_for..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto test_task = []() -> Task<void> {
        std::cout << " 50ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(50));
        
        std::cout << " 30ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(30));
        
        std::cout << " 20ms" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(20));
        
        std::cout << "" << std::endl;
    };
    
    sync_wait(test_task());
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << ": " << elapsed.count() << "ms" << std::endl;
    
    //  50+30+20=100ms
    TEST_EXPECT_TRUE(elapsed.count() >= 80 && elapsed.count() <= 150);
    
    std::cout << " sleep_for !" << std::endl;
}
int main() {
    std::cout << "=== FlowCoro sleep_for  ===" << std::endl;
    
    try {
        // 
        std::cout << "\n1. " << std::endl;
        test_basic_sleep_for();
        
        std::cout << "\n===  sleep_for ! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
