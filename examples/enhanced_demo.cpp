#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <numeric>
#include <vector>

// 测试缓存友好的缓冲区
void test_cache_friendly_buffer() {
    LOG_INFO("=== Testing Cache-Friendly Buffer ===");
    
    // 测试环形缓冲区
    flowcoro::CacheFriendlyRingBuffer<int, 1024> ring_buffer;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 批量写入测试
    std::vector<int> test_data(512);
    std::iota(test_data.begin(), test_data.end(), 1);
    
    size_t pushed = ring_buffer.push_batch(test_data.data(), test_data.size());
    LOG_INFO("Pushed %zu items to ring buffer", pushed);
    
    // 批量读取测试
    std::vector<int> read_data(512);
    size_t popped = ring_buffer.pop_batch(read_data.data(), read_data.size());
    LOG_INFO("Popped %zu items from ring buffer", popped);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    LOG_INFO("Ring buffer test completed in %ld microseconds", duration.count());
    
    // 测试字符串缓冲区
    flowcoro::StringBuffer str_buffer;
    str_buffer.append("Hello, ");
    str_buffer.append("World!");
    str_buffer.append_format(" Number: %d", 42);
    
    LOG_INFO("String buffer content: %s", str_buffer.c_str());
    LOG_INFO("String buffer size: %zu, capacity: %zu", str_buffer.size(), str_buffer.capacity());
}

// 测试内存池
void test_memory_pool() {
    LOG_INFO("=== Testing Cache-Friendly Memory Pool ===");
    
    flowcoro::CacheFriendlyMemoryPool<std::string> pool;
    
    std::vector<std::unique_ptr<std::string, std::function<void(std::string*)>>> objects;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 分配对象
    for (int i = 0; i < 100; ++i) {
        auto obj = pool.acquire("Object " + std::to_string(i));
        objects.push_back(std::move(obj));
    }
    
    auto pool_stats = pool.get_stats();
    LOG_INFO("Pool stats - Size: %zu, Allocated: %zu, Free: %zu, Utilization: %.2f%%",
             pool_stats.pool_size, pool_stats.allocated_count, 
             pool_stats.free_count, pool_stats.utilization);
    
    // 释放一半对象
    objects.erase(objects.begin(), objects.begin() + 50);
    
    pool_stats = pool.get_stats();
    LOG_INFO("After releasing 50 objects - Allocated: %zu, Free: %zu, Utilization: %.2f%%",
             pool_stats.allocated_count, pool_stats.free_count, pool_stats.utilization);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    LOG_INFO("Memory pool test completed in %ld microseconds", duration.count());
}

// 测试日志系统性能
void test_logging_performance() {
    LOG_INFO("=== Testing Logging Performance ===");
    
    const int num_logs = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_logs; ++i) {
        if (i % 1000 == 0) {
            LOG_INFO("Progress: %d/%d", i, num_logs);
        } else {
            LOG_DEBUG("Debug message %d with some data: %f", i, i * 3.14159);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    auto stats = flowcoro::GlobalLogger::get().get_stats();
    LOG_INFO("Logging performance test completed:");
    LOG_INFO("  - Duration: %ld ms", duration.count());
    LOG_INFO("  - Total logs: %lu", stats.total_logs);
    LOG_INFO("  - Dropped logs: %lu", stats.dropped_logs);
    LOG_INFO("  - Drop rate: %.2f%%", stats.drop_rate);
    LOG_INFO("  - Throughput: %.2f logs/ms", (double)num_logs / duration.count());
}

// 测试协程与日志集成
flowcoro::CoroTask test_coroutine_with_logging() {
    LOG_INFO("=== Testing Coroutine with Logging ===");
    
    LOG_DEBUG("Starting coroutine execution");
    
    // 测试异步Promise
    flowcoro::AsyncPromise<std::string> promise;
    
    // 在另一个线程中设置值
    flowcoro::GlobalThreadPool::get().enqueue_void([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        LOG_DEBUG("Setting promise value from worker thread");
        promise.set_value("Async result with logging!");
    });
    
    LOG_DEBUG("Waiting for async result...");
    std::string result = co_await promise;
    LOG_INFO("Received async result: %s", result.c_str());
    
    // 测试网络请求
    LOG_DEBUG("Starting network request...");
    std::string response = co_await flowcoro::CoroTask::execute_network_request<flowcoro::HttpRequest>("http://example.com/api");
    LOG_INFO("Network response received");
    
    LOG_DEBUG("Coroutine execution completed");
    co_return;
}

// 性能基准测试
void benchmark_buffer_vs_vector() {
    LOG_INFO("=== Buffer vs Vector Benchmark ===");
    
    const size_t iterations = 100000;
    const size_t batch_size = 64;
    
    // 测试标准vector
    auto start = std::chrono::high_resolution_clock::now();
    {
        std::vector<int> vec;
        for (size_t i = 0; i < iterations; ++i) {
            vec.push_back(static_cast<int>(i));
            if (vec.size() >= batch_size) {
                vec.clear();
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto vector_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // 测试缓存友好的环形缓冲区
    start = std::chrono::high_resolution_clock::now();
    {
        flowcoro::CacheFriendlyRingBuffer<int, 128> buffer;
        std::array<int, batch_size> batch;
        
        for (size_t i = 0; i < iterations; i += batch_size) {
            std::iota(batch.begin(), batch.end(), static_cast<int>(i));
            while (!buffer.push_batch(batch.data(), batch_size)) {
                // 缓冲区满，清空一些
                std::array<int, batch_size> temp;
                buffer.pop_batch(temp.data(), batch_size);
            }
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto buffer_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    LOG_INFO("Performance comparison:");
    LOG_INFO("  - Vector time: %ld µs", vector_time.count());
    LOG_INFO("  - Buffer time: %ld µs", buffer_time.count());
    LOG_INFO("  - Speedup: %.2fx", (double)vector_time.count() / buffer_time.count());
}

int main() {
    // 初始化日志系统
    flowcoro::GlobalLogger::get().set_level(flowcoro::LogLevel::LOG_DEBUG);
    
    LOG_INFO("FlowCoro Enhanced Demo with Logging and Cache-Friendly Buffers");
    LOG_INFO("================================================================");
    
    try {
        // 测试缓存友好的缓冲区
        test_cache_friendly_buffer();
        
        // 测试内存池
        test_memory_pool();
        
        // 测试日志性能
        test_logging_performance();
        
        // 性能基准测试
        benchmark_buffer_vs_vector();
        
        // 测试协程与日志集成
        auto coro_task = test_coroutine_with_logging();
        coro_task.resume();
        
        // 等待协程完成
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        LOG_INFO("All tests completed successfully!");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception occurred: %s", e.what());
        return 1;
    }
    
    // 显示最终日志统计
    auto final_stats = flowcoro::GlobalLogger::get().get_stats();
    LOG_INFO("Final logging statistics:");
    LOG_INFO("  - Total logs: %lu", final_stats.total_logs);
    LOG_INFO("  - Dropped logs: %lu", final_stats.dropped_logs);
    LOG_INFO("  - Drop rate: %.2f%%", final_stats.drop_rate);
    
    // 清理资源
    flowcoro::GlobalThreadPool::shutdown();
    flowcoro::GlobalLogger::shutdown();
    
    return 0;
}
