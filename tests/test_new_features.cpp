#include "../include/flowcoro.hpp"
#include "test_framework.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>
#include <queue>
#include <set>
#include <iomanip>

using namespace flowcoro;
using namespace flowcoro::test;

// 
#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << ": " << message << " ( " << __LINE__ << ")" << std::endl; \
            std::abort(); \
        } \
    } while(0)

#define ASSERT_EQ(a, b, message) \
    do { \
        if ((a) != (b)) { \
            std::cerr << ": " << message << " -  " << (b) << ",  " << (a) << " ( " << __LINE__ << ")" << std::endl; \
            std::abort(); \
        } \
    } while(0)

#define ASSERT_TRUE(condition, message) ASSERT(condition, message)
#define ASSERT_FALSE(condition, message) ASSERT(!(condition), message)
#define ASSERT_LE(a, b, message) ASSERT((a) <= (b), message)

// Channel
Task<void> test_channel_basic() {
    std::cout << ": Channel..." << std::endl;
    
    auto channel = make_channel<int>(1);
    
    // 
    bool sent = co_await channel->send(42);
    ASSERT_TRUE(sent, "");
    
    // 
    auto received = co_await channel->recv();
    ASSERT_TRUE(received.has_value(), "");
    ASSERT_EQ(received.value(), 42, "");
    
    std::cout << " Channel" << std::endl;
    co_return;
}

// Channel
Task<void> test_channel_buffered() {
    std::cout << ": Channel..." << std::endl;
    
    auto channel = make_channel<std::string>(3);
    
    // 3
    bool sent1 = co_await channel->send("message1");
    bool sent2 = co_await channel->send("message2");
    bool sent3 = co_await channel->send("message3");
    
    ASSERT_TRUE(sent1 && sent2 && sent3, "");
    
    // 
    auto recv1 = co_await channel->recv();
    auto recv2 = co_await channel->recv();
    auto recv3 = co_await channel->recv();
    
    ASSERT_TRUE(recv1.has_value() && recv1.value() == "message1", "");
    ASSERT_TRUE(recv2.has_value() && recv2.value() == "message2", "");
    ASSERT_TRUE(recv3.has_value() && recv3.value() == "message3", "");
    
    std::cout << " Channel" << std::endl;
    co_return;
}

// Channel
Task<void> test_channel_close() {
    std::cout << ": Channel..." << std::endl;
    
    auto channel = make_channel<int>(2);
    
    // 
    co_await channel->send(1);
    co_await channel->send(2);
    
    // 
    channel->close();
    
    // 
    bool sent = co_await channel->send(3);
    ASSERT_FALSE(sent, "");
    
    // 
    auto recv1 = co_await channel->recv();
    auto recv2 = co_await channel->recv();
    ASSERT_TRUE(recv1.has_value() && recv1.value() == 1, "");
    ASSERT_TRUE(recv2.has_value() && recv2.value() == 2, "");
    
    // nullopt
    auto recv3 = co_await channel->recv();
    ASSERT_FALSE(recv3.has_value(), "nullopt");
    
    std::cout << " Channel" << std::endl;
    co_return;
}

// 
Task<void> test_performance() {
    std::cout << ": ..." << std::endl;
    
    auto channel = make_channel<int>(100);
    const int message_count = 100; // 
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto sender = [&]() -> Task<void> {
        for (int i = 0; i < message_count; ++i) {
            co_await channel->send(i);
        }
        channel->close();
        co_return;
    };
    
    auto receiver = [&]() -> Task<void> {
        int count = 0;
        while (true) {
            auto value = co_await channel->recv();
            if (!value.has_value()) break;
            count++;
        }
        ASSERT_EQ(count, message_count, "");
        co_return;
    };
    
    auto send_task = sender();
    auto recv_task = receiver();
    
    co_await send_task;
    co_await recv_task;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (duration.count() > 0) {
        double throughput = (message_count * 1000.0) / duration.count();
        std::cout << "Channel: " << throughput << " /" << std::endl;
    } else {
        std::cout << " < 1ms" << std::endl;
    }
    
    std::cout << " " << std::endl;
    co_return;
}

// 
Task<void> test_producer_consumer_pattern() {
    std::cout << "Channel..." << std::endl;
    
    // 10Channel
    auto channel = std::make_shared<Channel<int>>(10);
    
    // 
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};
    
    // ID
    std::mutex thread_ids_mutex;
    std::set<std::thread::id> producer_threads;
    std::set<std::thread::id> consumer_threads;
    
    // 
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int ITEMS_PER_PRODUCER = 5;
    
    std::cout << " " << NUM_PRODUCERS << " ..." << std::endl;
    std::cout << " " << NUM_CONSUMERS << " ..." << std::endl;
    
    std::vector<Task<void>> tasks;
    
    // 
    for (int producer_id = 0; producer_id < NUM_PRODUCERS; ++producer_id) {
        auto producer_task = [channel, producer_id, ITEMS_PER_PRODUCER, 
                             &produced_count, &produced_sum, 
                             &thread_ids_mutex, &producer_threads]() -> Task<void> {
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                producer_threads.insert(std::this_thread::get_id());
            }
            
            std::cout << " " << producer_id << " : " << std::this_thread::get_id() << std::endl;
            
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = producer_id * 100 + i;
                
                try {
                    std::cout << " " << producer_id << " : " << value << std::endl;
                    bool sent = co_await channel->send(value);
                    if (sent) {
                        produced_count.fetch_add(1);
                        produced_sum.fetch_add(value);
                        std::cout << " " << producer_id << " : " << value 
                                 << " (: " << produced_count.load() << ")" << std::endl;
                    } else {
                        std::cout << " " << producer_id << " Channel" << std::endl;
                        break;
                    }
                    
                    // 
                    co_await sleep_for(std::chrono::milliseconds(10));
                } catch (const std::exception& e) {
                    std::cout << " " << producer_id << " : " << e.what() << std::endl;
                    break;
                }
            }
            
            std::cout << " " << producer_id << " : " << ITEMS_PER_PRODUCER << std::endl;
        };
        
        tasks.emplace_back(producer_task());
    }
    
    // 
    for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
        auto consumer_task = [channel, consumer_id, NUM_PRODUCERS, ITEMS_PER_PRODUCER,
                             &consumed_count, &consumed_sum, 
                             &thread_ids_mutex, &consumer_threads]() -> Task<void> {
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                consumer_threads.insert(std::this_thread::get_id());
            }
            
            std::cout << " " << consumer_id << " : " << std::this_thread::get_id() << std::endl;
            
            int max_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
            
            while (consumed_count.load() < max_items) {
                try {
                    std::cout << " " << consumer_id << " ..." << std::endl;
                    auto result = co_await channel->recv();
                    
                    if (!result.has_value()) {
                        std::cout << " " << consumer_id << " Channel" << std::endl;
                        break;
                    }
                    
                    int value = result.value();
                    consumed_count.fetch_add(1);
                    consumed_sum.fetch_add(value);
                    
                    std::cout << " " << consumer_id << " : " << value 
                             << " (: " << consumed_count.load() << "/" << max_items << ")" << std::endl;
                    
                    // 
                    co_await sleep_for(std::chrono::milliseconds(5));
                    
                    // 
                    if (consumed_count.load() >= max_items) {
                        std::cout << " " << consumer_id << " " << std::endl;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cout << " " << consumer_id << " : " << e.what() << std::endl;
                    break;
                }
            }
            
            std::cout << " " << consumer_id << " " << std::endl;
        };
        
        tasks.emplace_back(consumer_task());
    }
    
    // Channel
    auto monitor_task = [channel, NUM_PRODUCERS, ITEMS_PER_PRODUCER, &produced_count, &consumed_count]() -> Task<void> {
        std::cout << "..." << std::endl;
        
        int max_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
        int timeout_cycles = 0;
        
        while (true) {
            co_await sleep_for(std::chrono::milliseconds(100));
            
            int prod = produced_count.load();
            int cons = consumed_count.load();
            
            std::cout << ": =" << prod << "/" << max_items 
                     << ", =" << cons << "/" << max_items << std::endl;
            
            // 
            if (cons >= max_items) {
                std::cout << ": Channel" << std::endl;
                break;
            }
            
            // 
            if (prod >= max_items) {
                timeout_cycles++;
                std::cout << ":  (:" << timeout_cycles << ")" << std::endl;
                
                // 5
                if (timeout_cycles > 50) {
                    std::cout << ": Channel" << std::endl;
                    break;
                }
            }
        }
        
        channel->close();
        std::cout << ": Channel" << std::endl;
    };
    
    tasks.emplace_back(monitor_task());
    
    std::cout << "..." << std::endl;
    
    // 
    for (auto& task : tasks) {
        try {
            co_await task;
        } catch (const std::exception& e) {
            std::cout << ": " << e.what() << std::endl;
        }
    }
    
    // 
    std::cout << "\n===  ===" << std::endl;
    std::cout << ": " << produced_count.load() << " (: " << NUM_PRODUCERS * ITEMS_PER_PRODUCER << ")" << std::endl;
    std::cout << ": " << consumed_count.load() << std::endl;
    std::cout << ": " << produced_sum.load() << std::endl;
    std::cout << ": " << consumed_sum.load() << std::endl;
    std::cout << ": " << producer_threads.size() << std::endl;
    std::cout << ": " << consumer_threads.size() << std::endl;
    
    // 
    TEST_EXPECT_EQ(produced_count.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    TEST_EXPECT_EQ(consumed_count.load(), produced_count.load());
    TEST_EXPECT_EQ(consumed_sum.load(), produced_sum.load());
    
    // 
    TEST_EXPECT_TRUE(producer_threads.size() >= 1);
    TEST_EXPECT_TRUE(consumer_threads.size() >= 1);
    
    std::cout << "Channel" << std::endl;
}

// 
Task<void> run_all_tests() {
    std::cout << "FlowCoro..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        co_await test_channel_basic();
        co_await test_channel_buffered();
        co_await test_channel_close();
        co_await test_performance();
        co_await test_producer_consumer_pattern();  // 
        
        std::cout << std::endl;
        std::cout << " " << std::endl;
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

int main() {
    try {
        sync_wait(run_all_tests());
        std::cout << "" << std::endl;
        return 0;
    } catch (...) {
        std::cerr << "" << std::endl;
        return 1;
    }
}
