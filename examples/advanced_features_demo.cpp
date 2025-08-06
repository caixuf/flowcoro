#include "flowcoro.hpp"
#include "flowcoro/channel.h"
#include "flowcoro/async_sync.h"
#include <iostream>
#include <chrono>

using namespace flowcoro;

// 同步原语示例：多协程访问共享资源
sync::AsyncMutex shared_mutex;
int shared_counter = 0;

Task<void> increment_worker(int worker_id, int iterations) {
    std::cout << "Worker " << worker_id << " 开始工作，执行 " << iterations << " 次操作" << std::endl;
    for (int i = 0; i < iterations; ++i) {
        std::cout << "Worker " << worker_id << " 尝试获取锁..." << std::endl;
        // 获取锁
        co_await shared_mutex.lock();
        
        std::cout << "Worker " << worker_id << " 获得锁" << std::endl;
        
        // 临界区：访问共享资源
        int old_value = shared_counter;
        co_await sleep_for(std::chrono::milliseconds(10)); // 稍微增加工作时间
        shared_counter = old_value + 1;
        
        std::cout << "Worker " << worker_id << " 增量: " 
                  << old_value << " -> " << shared_counter << std::endl;
        
        // 释放锁
        shared_mutex.unlock();
        std::cout << "Worker " << worker_id << " 释放锁" << std::endl;
        
        co_await sleep_for(std::chrono::milliseconds(10)); // 稍微增加间隔
    }
    std::cout << "Worker " << worker_id << " 完成所有工作" << std::endl;
    co_return;
}

// 信号量示例：限制并发数量
sync::AsyncSemaphore resource_pool(3); // 最多3个并发资源

Task<void> use_resource(int worker_id) {
    std::cout << "Worker " << worker_id << " 等待资源..." << std::endl;
    
    // 获取资源
    co_await resource_pool.acquire();
    
    std::cout << "Worker " << worker_id << " 获得资源，开始工作，剩余资源: " 
              << resource_pool.available() << std::endl;
    
    // 使用资源
    co_await sleep_for(std::chrono::milliseconds(200));
    
    std::cout << "Worker " << worker_id << " 完成工作，释放资源" << std::endl;
    
    // 释放资源
    resource_pool.release();
    
    std::cout << "Worker " << worker_id << " 资源已释放，当前可用: " 
              << resource_pool.available() << std::endl;
    
    co_return;
}

Task<void> channel_demo() {
    std::cout << "\n=== Channel 生产者-消费者示例 ===" << std::endl;
    
    // 创建容量为5的channel
    auto channel = make_channel<int>(5);
    
    auto producer_task = [&]() -> Task<void> {
        for (int i = 1; i <= 5; ++i) {
            bool sent = co_await channel->send(i);
            if (!sent) {
                std::cout << "Channel已关闭，停止生产" << std::endl;
                break;
            }
            std::cout << "生产: " << i << std::endl;
            co_await sleep_for(std::chrono::milliseconds(50));
        }
        std::cout << "生产者完成" << std::endl;
        co_return;
    }();
    
    auto consumer_task = [&]() -> Task<void> {
        int count = 0;
        while (count < 5) {
            auto value = co_await channel->recv();
            if (!value.has_value()) {
                std::cout << "消费者检测到channel关闭" << std::endl;
                break;
            }
            std::cout << "消费: " << value.value() << std::endl;
            count++;
            co_await sleep_for(std::chrono::milliseconds(30));
        }
        std::cout << "消费者完成" << std::endl;
        co_return;
    }();
    
    // 等待生产者和消费者完成
    co_await producer_task;
    co_await consumer_task;
    
    std::cout << "Channel演示完成" << std::endl;
    co_return;
}

Task<void> mutex_demo() {
    std::cout << "\n=== AsyncMutex 共享资源示例 ===" << std::endl;
    
    shared_counter = 0; // 重置计数器
    
    // 启动3个工作协程，每个执行2次
    auto worker1 = increment_worker(1, 2);
    auto worker2 = increment_worker(2, 2);
    auto worker3 = increment_worker(3, 2);
    
    // 等待所有工作完成
    co_await worker1;
    co_await worker2;
    co_await worker3;
    
    std::cout << "最终计数器值: " << shared_counter << std::endl;
    std::cout << "Mutex演示完成" << std::endl;
    co_return;
}

Task<void> semaphore_demo() {
    std::cout << "\n=== AsyncSemaphore 资源池示例 ===" << std::endl;
    
    // 启动4个工作协程（但只有3个资源）
    auto worker1 = use_resource(1);
    auto worker2 = use_resource(2);
    auto worker3 = use_resource(3);
    auto worker4 = use_resource(4);
    
    // 等待所有工作完成
    co_await worker1;
    co_await worker2;
    co_await worker3;
    co_await worker4;
    
    std::cout << "Semaphore演示完成" << std::endl;
    co_return;
}

Task<void> advanced_features_demo() {
    std::cout << "FlowCoro 高级特性演示" << std::endl;
    std::cout << "==================" << std::endl;
    
    // 运行各种示例
    co_await channel_demo();
    co_await mutex_demo();
    co_await semaphore_demo();
    
    std::cout << "\n所有演示完成！" << std::endl;
    co_return;
}

int main() {
    try {
        sync_wait(advanced_features_demo());
        std::cout << "程序正常结束" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
