#pragma once
#include "flowcoro.h"
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace flowcoro {

// 简化版HTTP请求类，不依赖libcurl
class HttpRequest : public INetworkRequest {
public:
    HttpRequest() {
        // 启动工作线程
        worker_thread_ = std::thread(&HttpRequest::worker_thread_func, this);
    }
    
    ~HttpRequest() override {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_worker_thread_ = true;
        }
        queue_condition_.notify_one();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    void request(const std::string& url, const std::function<void(const std::string&)>& callback) override {
        // 添加到请求队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            requests_queue_.push({url, callback});
        }
        queue_condition_.notify_one();
    }
    
private:
    struct RequestInfo {
        std::string url;
        std::function<void(const std::string&)> callback;
    };
    
    std::thread worker_thread_;
    std::mutex queue_mutex_;
    std::queue<RequestInfo> requests_queue_;
    std::condition_variable queue_condition_;
    bool stop_worker_thread_ = false;
    
    // 工作线程函数
    void worker_thread_func() {
        while (true) {
            RequestInfo request;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait(lock, [this] {
                    return stop_worker_thread_ || !requests_queue_.empty();
                });
                
                if (stop_worker_thread_ && requests_queue_.empty()) {
                    break;
                }
                
                if (!requests_queue_.empty()) {
                    request = requests_queue_.front();
                    requests_queue_.pop();
                } else {
                    continue;
                }
            }
            
            // 模拟网络请求延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 模拟响应
            if (request.callback) {
                std::string response = "Mock HTTP response for: " + request.url;
                request.callback(response);
            }
        }
    }
};

} // namespace flowcoro
