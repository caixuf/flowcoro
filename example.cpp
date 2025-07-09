#pragma once
#include <string>
#include <functional>
#include <thread>
#include <future>

namespace flowcoro {

class CoroTask {
public:
    // 添加网络请求静态方法
    template<typename ResponseType = std::string>
    static CoroTask execute_network_request(const std::string& url, 
                                           std::function<void(ResponseType)> callback) {
        CoroTask task;
        std::thread([url, callback]() {
            // 模拟网络请求
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // 模拟响应数据
            ResponseType response;
            if constexpr (std::is_same_v<ResponseType, std::string>) {
                response = "HTTP/1.1 200 OK\nContent-Length: 13\n\nHello, World!";
            } else if constexpr (std::is_same_v<ResponseType, std::vector<char>>) {
                std::string str_resp = "HTTP/1.1 200 OK\nContent-Length: 13\n\nHello, World!";
                response = std::vector<char>(str_resp.begin(), str_resp.end());
            }
            
            // 在主线程执行回调
            std::promise<void> p;
            std::future<void> f = p.get_future();
            
            // 模拟事件循环派发
            std::thread([callback, response, p = std::move(p)]() mutable {
                callback(response);
                p.set_value();
            }).detach();
            
            // 返回异步任务
            return task;
        }).detach();
        
        return task;
    }
};

} // namespace flowcoro
#include "flowcoro.h"
#include "http_request.h"
#include <iostream>

// 使用自定义网络请求的协程
flowcoro::CoroTask network_coro() {
    std::cout << "Starting network request..." << std::endl;
    
    // 执行网络请求并等待结果
    std::string response = co_await flowcoro::CoroTask::execute_network_request<>("http://example.com");
    
    std::cout << "Response received, size: " << response.size() << " bytes" << std::endl;
    
    std::cout << "Network request completed" << std::endl;
    co_return;
}

int main() {
    // 创建并运行网络请求协程
    auto co = network_coro();
    co.resume();
    
    // 保持程序运行直到所有异步操作完成
    std::cin.get();
    return 0;
}
