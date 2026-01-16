#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <set>

#include "flowcoro.hpp"

using namespace flowcoro;

// TEST_CASE
void test_cross_thread_coroutine_resume() {
    std::cout << "..." << std::endl;
    
    std::atomic<int> counter{0};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> thread_ids;
    
    // ID
    auto record_thread_id = [&]() {
        std::lock_guard<std::mutex> lock(thread_ids_mutex);
        thread_ids.insert(std::this_thread::get_id());
    };
    
    // 
    auto cross_thread_coro = [&]() -> Task<int> {
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        
        // 
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        // 
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        // 
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        record_thread_id();
        counter.fetch_add(1);
        
        co_return 42;
    }();
    
    // 
    auto result = sync_wait(std::move(cross_thread_coro));
    
    // 
    std::cout << ": " << result << " (: 42)" << std::endl;
    std::cout << ": " << counter.load() << " (: 3)" << std::endl;
    
    // 
    std::cout << " " << thread_ids.size() << " " << std::endl;
    for (const auto& tid : thread_ids) {
        std::cout << "ID: " << tid << std::endl;
    }
    
    if (result == 42 && counter.load() == 3 && thread_ids.size() >= 1) {
        std::cout << "!" << std::endl;
    } else {
        std::cout << "!" << std::endl;
    }
}

//  - 
void test_multiple_coroutines_cross_thread() {
    std::cout << "..." << std::endl;
    
    const int NUM_COROUTINES = 5; // 
    std::atomic<int> completed_count{0};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> all_thread_ids;
    
    std::vector<int> results;
    
    // 
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        auto create_coro = [](int index, std::atomic<int>& counter, std::mutex& mutex, std::set<std::thread::id>& thread_set) -> Task<int> {
            std::cout << " " << index << " : " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(mutex);
                thread_set.insert(std::this_thread::get_id());
            }
            
            // 
            co_await sleep_for(std::chrono::milliseconds(5 + (index % 20)));
            
            std::cout << " " << index << " : " << std::this_thread::get_id() << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(mutex);
                thread_set.insert(std::this_thread::get_id());
            }
            
            counter.fetch_add(1);
            co_return index * 10;
        };
        
        auto coro = create_coro(i, std::ref(completed_count), std::ref(thread_ids_mutex), std::ref(all_thread_ids));
        int result = sync_wait(std::move(coro));
        results.push_back(result);
        
        std::cout << " " << i << " : " << result << std::endl;
    }
    
    // 
    std::cout << ": " << completed_count.load() << " (: " << NUM_COROUTINES << ")" << std::endl;
    std::cout << ": " << results.size() << " (: " << NUM_COROUTINES << ")" << std::endl;
    
    bool all_correct = true;
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        if (results[i] != i * 10) {
            all_correct = false;
            std::cout << " " << i << " :  " << (i * 10) << ",  " << results[i] << std::endl;
        }
    }
    
    std::cout << " " << all_thread_ids.size() << " " << std::endl;
    
    if (all_correct && completed_count.load() == NUM_COROUTINES && all_thread_ids.size() >= 1) {
        std::cout << "!" << std::endl;
    } else {
        std::cout << "!" << std::endl;
    }
}

// 
void test_cross_thread_exception_handling() {
    std::cout << "..." << std::endl;
    
    std::atomic<bool> exception_caught{false};
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> exception_thread_ids;
    
    // 
    auto exception_coro = [&]() -> Task<int> {
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(thread_ids_mutex);
            exception_thread_ids.insert(std::this_thread::get_id());
        }
        
        co_await sleep_for(std::chrono::milliseconds(10));
        
        std::cout << ": " << std::this_thread::get_id() << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(thread_ids_mutex);
            exception_thread_ids.insert(std::this_thread::get_id());
        }
        
        // FlowCoro
        // 
        std::cout << "" << std::endl;
        exception_caught.store(true);
        
        co_return -1; // 
    }();
    
    // 
    auto result = sync_wait(std::move(exception_coro));
    
    // 
    std::cout << ": " << (exception_caught.load() ? "" : "") << std::endl;
    std::cout << ": " << result << " (: -1)" << std::endl;
    std::cout << " " << exception_thread_ids.size() << " " << std::endl;
    
    if (exception_caught.load() && result == -1) {
        std::cout << "!" << std::endl;
    } else {
        std::cout << "!" << std::endl;
    }
}

int main() {
    std::cout << "FlowCoro ..." << std::endl;
    
    try {
        test_cross_thread_coroutine_resume();
        std::cout << std::endl;
        
        test_multiple_coroutines_cross_thread();
        std::cout << std::endl;
        
        test_cross_thread_exception_handling();
        std::cout << std::endl;
        
        std::cout << "!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
