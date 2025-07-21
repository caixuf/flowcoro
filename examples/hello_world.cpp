#include "../include/flowcoro.hpp"
#include <iostream>
#include <thread>

// 简单的异步任务
flowcoro::Task<std::string> fetch_data(const std::string& url) {
    // 使用简单的sleep而不是协程sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return "Data from " + url;
}

int main() {
    try {
        std::cout << "=== FlowCoro Hello World ===" << std::endl;
        
        // 创建并运行简单的协程
        auto task = fetch_data("https://api.example.com");
        auto result = task.get();
        
        std::cout << "收到数据: " << result << std::endl;
        std::cout << "Hello FlowCoro! 协程执行完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}