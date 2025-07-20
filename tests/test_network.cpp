#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "../include/flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// Mock HTTP 请求类
class MockHttpRequest : public INetworkRequest {
private:
    std::string url_;
    std::string method_;
    std::string response_data_;
    int status_code_;
    
public:
    MockHttpRequest(const std::string& url, const std::string& method = "GET") 
        : url_(url), method_(method), status_code_(200) {
        // 模拟不同的响应
        if (url_.find("users") != std::string::npos) {
            response_data_ = R"({"id": 1, "name": "Test User", "email": "test@example.com"})";
        } else if (url_.find("orders") != std::string::npos) {
            response_data_ = R"({"id": 1, "product": "Test Product", "amount": 99.99})";
        } else if (url_.find("error") != std::string::npos) {
            status_code_ = 404;
            response_data_ = R"({"error": "Not found"})";
        } else {
            response_data_ = R"({"message": "Hello World"})";
        }
    }
    
    Task<std::string> execute() override {
        // 模拟网络延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        co_return response_data_;
    }
    
    void set_header(const std::string& key, const std::string& value) override {
        // 模拟设置请求头
    }
    
    void set_body(const std::string& body) override {
        // 模拟设置请求体
    }
    
    int get_status_code() const override {
        return status_code_;
    }
    
    std::string get_url() const { return url_; }
    std::string get_method() const { return method_; }
};

// 网络客户端包装器
class NetworkClient {
public:
    Task<std::string> get(const std::string& url) {
        auto request = std::make_unique<MockHttpRequest>(url, "GET");
        auto response = co_await request->execute();
        co_return response;
    }
    
    Task<std::string> post(const std::string& url, const std::string& data) {
        auto request = std::make_unique<MockHttpRequest>(url, "POST");
        request->set_body(data);
        auto response = co_await request->execute();
        co_return response;
    }
    
    // 测试多个并发请求
    Task<std::vector<std::string>> batch_requests(const std::vector<std::string>& urls) {
        std::vector<Task<std::string>> tasks;
        
        // 启动所有请求
        for (const auto& url : urls) {
            tasks.push_back(get(url));
        }
        
        // 等待所有请求完成
        std::vector<std::string> results;
        for (auto& task : tasks) {
            results.push_back(co_await std::move(task));
        }
        
        co_return results;
    }
};

// 测试基本的 HTTP GET 请求
TEST_CASE(http_get_request) {
    NetworkClient client;
    
    auto response_task = client.get("https://api.example.com/users/1");
    auto response = sync_wait(std::move(response_task));
    
    TEST_EXPECT_TRUE(response.find("Test User") != std::string::npos);
    TEST_EXPECT_TRUE(response.find("test@example.com") != std::string::npos);
}

// 测试 HTTP POST 请求
TEST_CASE(http_post_request) {
    NetworkClient client;
    
    std::string post_data = R"({"name": "New User", "email": "new@example.com"})";
    auto response_task = client.post("https://api.example.com/users", post_data);
    auto response = sync_wait(std::move(response_task));
    
    TEST_EXPECT_FALSE(response.empty());
}

// 测试多个并发请求
TEST_CASE(concurrent_http_requests) {
    NetworkClient client;
    
    std::vector<std::string> urls = {
        "https://api.example.com/users/1",
        "https://api.example.com/users/2", 
        "https://api.example.com/orders/1",
        "https://api.example.com/orders/2"
    };
    
    auto batch_task = client.batch_requests(urls);
    auto responses = sync_wait(std::move(batch_task));
    
    TEST_EXPECT_EQ(responses.size(), 4);
    for (const auto& response : responses) {
        TEST_EXPECT_FALSE(response.empty());
    }
}

// 测试错误处理
TEST_CASE(http_error_handling) {
    NetworkClient client;
    
    auto response_task = client.get("https://api.example.com/error");
    auto response = sync_wait(std::move(response_task));
    
    TEST_EXPECT_TRUE(response.find("error") != std::string::npos);
}

// 测试网络请求池化
TEST_CASE(request_pooling) {
    const int num_requests = 20;
    NetworkClient client;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 创建多个任务
    std::vector<Task<std::string>> tasks;
    for (int i = 0; i < num_requests; ++i) {
        tasks.push_back(client.get("https://api.example.com/test/" + std::to_string(i)));
    }
    
    // 等待所有任务完成
    int completed = 0;
    for (auto& task : tasks) {
        auto response = sync_wait(std::move(task));
        if (!response.empty()) {
            completed++;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    TEST_EXPECT_EQ(completed, num_requests);
    std::cout << "Completed " << num_requests << " requests in " << duration.count() << "ms" << std::endl;
}

// 测试请求超时处理
TEST_CASE(request_timeout) {
    class TimeoutRequest : public INetworkRequest {
    public:
        Task<std::string> execute() override {
            // 模拟超时
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            co_return "timeout_response";
        }
        
        void set_header(const std::string&, const std::string&) override {}
        void set_body(const std::string&) override {}
        int get_status_code() const override { return 200; }
    };
    
    auto request = std::make_unique<TimeoutRequest>();
    auto response_task = request->execute();
    
    // 使用超时等待
    auto start = std::chrono::high_resolution_clock::now();
    auto response = sync_wait(std::move(response_task));
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    TEST_EXPECT_TRUE(duration.count() >= 100); // 应该至少等待100ms
    TEST_EXPECT_EQ(response, "timeout_response");
}

// 测试流式数据处理
TEST_CASE(streaming_data) {
    class StreamingRequest : public INetworkRequest {
    public:
        Task<std::string> execute() override {
            std::string result;
            
            // 模拟分块接收数据
            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                result += "chunk_" + std::to_string(i) + " ";
            }
            
            co_return result;
        }
        
        void set_header(const std::string&, const std::string&) override {}
        void set_body(const std::string&) override {}
        int get_status_code() const override { return 200; }
    };
    
    auto request = std::make_unique<StreamingRequest>();
    auto response_task = request->execute();
    auto response = sync_wait(std::move(response_task));
    
    TEST_EXPECT_TRUE(response.find("chunk_0") != std::string::npos);
    TEST_EXPECT_TRUE(response.find("chunk_4") != std::string::npos);
}

int main() {
    TEST_SUITE("FlowCoro Network Functionality Tests");
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
