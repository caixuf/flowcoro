/**
 * @file network_example.cpp
 * @brief FlowCoro 网络IO层示例
 */

#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <thread>

// 简单的Echo服务器示例
flowcoro::Task<void> echo_server() {
    std::cout << "启动Echo服务器..." << std::endl;
    
    auto& loop = flowcoro::net::GlobalEventLoop::get();
    flowcoro::net::TcpServer server(&loop);
    
    // 设置连接处理器
    server.set_connection_handler([](std::unique_ptr<flowcoro::net::Socket> client) -> flowcoro::Task<void> {
        std::cout << "新客户端连接" << std::endl;
        
        try {
            // 读取客户端数据
            auto data = co_await client->read_line();
            std::cout << "收到消息: " << data << std::endl;
            
            // 回显消息
            std::string response = "Echo: " + data + "\n";
            co_await client->write_string(response);
            std::cout << "已回显消息" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "处理客户端时出错: " << e.what() << std::endl;
        }
        
        co_return;
    });
    
    // 启动服务器
    co_await server.listen("127.0.0.1", 8080);
    
    std::cout << "Echo服务器已停止" << std::endl;
    co_return;
}

// 事件循环任务
flowcoro::Task<void> run_event_loop() {
    auto& loop = flowcoro::net::GlobalEventLoop::get();
    co_await loop.run();
}

int main() {
    std::cout << "FlowCoro 网络IO层示例" << std::endl;
    std::cout << "=====================" << std::endl;
    
    try {
        // 创建服务器任务
        auto server_task = echo_server();
        
        // 创建事件循环任务
        auto loop_task = run_event_loop();
        
        std::cout << "服务器将在 127.0.0.1:8080 监听" << std::endl;
        std::cout << "可以使用 telnet 127.0.0.1 8080 测试" << std::endl;
        std::cout << "按 Ctrl+C 退出" << std::endl;
        
        // 让程序运行一段时间
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        std::cout << "示例运行完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
