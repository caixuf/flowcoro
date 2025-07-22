#include <iostream>
#include <thread>
#include <chrono>
#include "include/flowcoro.hpp"

using namespace flowcoro;

// 测试跨线程协程恢复保护
Task<void> cross_thread_test() {
    std::cout << "协程开始执行在线程: " << std::this_thread::get_id() << std::endl;
    
    // 这会尝试跨线程恢复协程
    co_await sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "协程恢复执行在线程: " << std::this_thread::get_id() << std::endl;
    std::cout << "如果你看到这条消息，说明协程没有被跨线程恢复阻止" << std::endl;
}

int main() {
    std::cout << "=== FlowCoro 跨线程安全测试 ===" << std::endl;
    std::cout << "主线程ID: " << std::this_thread::get_id() << std::endl;
    
    auto task = cross_thread_test();
    
    try {
        sync_wait(std::move(task));
        std::cout << "✅ 测试完成，没有段错误！" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "❌ 异常: " << e.what() << std::endl;
    }
    
    return 0;
}
