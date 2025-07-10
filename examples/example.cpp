#include <flowcoro.hpp>
#include <iostream>
#include <atomic>
#include <chrono>

// 模拟网络请求类
class ExampleHttpRequest : public flowcoro::INetworkRequest {
public:
    void request(const std::string& url, const std::function<void(const std::string&)>& callback) override {
        // 使用无锁线程池执行网络请求
        flowcoro::GlobalThreadPool::get().enqueue_void([url, callback]() {
            // 模拟网络延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // 模拟响应
            std::string response = "HTTP/1.1 200 OK\nContent-Length: 13\n\nHello from " + url;
            callback(response);
        });
    }
};

// 演示无锁队列和协程的结合使用
flowcoro::CoroTask producer_consumer_demo() {
    std::cout << "=== Producer-Consumer Demo ===" << std::endl;
    
    lockfree::Queue<int> queue;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    const int total_items = 100;
    
    // 启动生产者任务
    auto producer_future = flowcoro::GlobalThreadPool::get().enqueue([&queue, &produced_count, total_items]() {
        for (int i = 0; i < total_items; ++i) {
            queue.enqueue(i);
            produced_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        std::cout << "Producer finished: " << produced_count.load() << " items" << std::endl;
    });
    
    // 启动消费者任务
    auto consumer_future = flowcoro::GlobalThreadPool::get().enqueue([&queue, &consumed_count, total_items]() {
        int value;
        while (consumed_count.load(std::memory_order_acquire) < total_items) {
            if (queue.dequeue(value)) {
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
        std::cout << "Consumer finished: " << consumed_count.load() << " items" << std::endl;
    });
    
    // 等待完成
    producer_future.get();
    consumer_future.get();
    
    std::cout << "Producer-Consumer demo completed!" << std::endl;
    co_return;
}

// 使用自定义网络请求的协程
flowcoro::CoroTask network_coro() {
    std::cout << "=== Network Request Demo ===" << std::endl;
    std::cout << "Starting network request..." << std::endl;
    
    // 执行网络请求并等待结果
    std::string response = co_await flowcoro::CoroTask::execute_network_request<ExampleHttpRequest>("http://example.com");
    
    std::cout << "Response received: " << response << std::endl;
    
    std::cout << "Network request completed!" << std::endl;
    co_return;
}

// 演示并行任务处理
flowcoro::CoroTask parallel_tasks_demo() {
    std::cout << "=== Parallel Tasks Demo ===" << std::endl;
    
    std::atomic<int> task_counter{0};
    std::vector<std::future<void>> futures;
    
    // 启动多个并行任务
    for (int i = 0; i < 10; ++i) {
        futures.push_back(flowcoro::GlobalThreadPool::get().enqueue([&task_counter, i]() {
            // 模拟工作
            std::this_thread::sleep_for(std::chrono::milliseconds(100 + (i * 50)));
            int count = task_counter.fetch_add(1, std::memory_order_relaxed);
            std::cout << "Task " << i << " completed (total: " << count + 1 << ")" << std::endl;
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    std::cout << "All parallel tasks completed!" << std::endl;
    co_return;
}

// 演示无锁异步promise
flowcoro::CoroTask async_promise_demo() {
    std::cout << "=== Async Promise Demo ===" << std::endl;
    
    flowcoro::AsyncPromise<std::string> promise;
    
    // 在另一个线程中异步设置值
    flowcoro::GlobalThreadPool::get().enqueue_void([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        promise.set_value("Async result from thread pool!");
    });
    
    std::cout << "Waiting for async result..." << std::endl;
    std::string result = co_await promise;
    std::cout << "Got result: " << result << std::endl;
    
    co_return;
}

int main() {
    std::cout << "FlowCoro Lockfree Programming Demo" << std::endl;
    std::cout << "===================================" << std::endl;
    
    // 创建并运行各种演示协程
    auto demo1 = producer_consumer_demo();
    auto demo2 = network_coro();
    auto demo3 = parallel_tasks_demo();
    auto demo4 = async_promise_demo();
    
    // 启动所有演示
    demo1.resume();
    demo2.resume();
    demo3.resume();
    demo4.resume();
    
    // 等待一段时间让所有任务完成
    std::cout << "\nWaiting for all demos to complete..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 显示线程池状态
    std::cout << "\nThread pool status:" << std::endl;
    std::cout << "Active threads: " << flowcoro::GlobalThreadPool::get().active_thread_count() << std::endl;
    
    // 关闭调度器
    flowcoro::GlobalThreadPool::shutdown();
    
    std::cout << "\nAll demos completed!" << std::endl;
    return 0;
}
