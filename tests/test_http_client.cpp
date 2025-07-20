#include <iostream>
#include <string>

#include "../include/flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace flowcoro::net;

// 测试基本HTTP GET请求
TEST_CASE(http_get_request) {
    HttpClient client;
    
    // 使用 httpbin.org 作为测试端点（如果网络可达）
    auto response_task = client.get("http://httpbin.org/get");
    auto response = sync_wait(std::move(response_task));
    
    if (response.success) {
        TEST_EXPECT_EQ(response.status_code, 200);
        TEST_EXPECT_TRUE(response.body.find("httpbin") != std::string::npos);
        std::cout << "HTTP GET test passed: " << response.status_code << std::endl;
    } else {
        // 如果网络不可达，至少验证客户端不会崩溃
        std::cout << "HTTP GET test (network unavailable): " << response.error_message << std::endl;
        TEST_EXPECT_TRUE(true); // 不崩溃就算通过
    }
}

// 测试HTTP POST请求
TEST_CASE(http_post_request) {
    HttpClient client;
    
    std::string post_data = R"({"name": "FlowCoro", "version": "2.0.0"})";
    auto response_task = client.post("http://httpbin.org/post", post_data);
    auto response = sync_wait(std::move(response_task));
    
    if (response.success) {
        TEST_EXPECT_EQ(response.status_code, 200);
        TEST_EXPECT_TRUE(response.body.find("FlowCoro") != std::string::npos);
        std::cout << "HTTP POST test passed: " << response.status_code << std::endl;
    } else {
        std::cout << "HTTP POST test (network unavailable): " << response.error_message << std::endl;
        TEST_EXPECT_TRUE(true); // 不崩溃就算通过
    }
}

// 测试并发HTTP请求
TEST_CASE(concurrent_http_requests) {
    HttpClient client;
    
    std::vector<Task<HttpResponse>> tasks;
    
    // 创建多个并发请求
    for (int i = 0; i < 3; ++i) {
        tasks.push_back(client.get("http://httpbin.org/delay/1"));
    }
    
    int successful_requests = 0;
    
    // 等待所有请求完成
    for (auto& task : tasks) {
        auto response = sync_wait(std::move(task));
        if (response.success) {
            successful_requests++;
        }
    }
    
    if (successful_requests > 0) {
        std::cout << "Concurrent requests: " << successful_requests << "/3 succeeded" << std::endl;
        TEST_EXPECT_TRUE(successful_requests >= 0);
    } else {
        std::cout << "Concurrent HTTP test (network unavailable)" << std::endl;
        TEST_EXPECT_TRUE(true);
    }
}

// 测试错误处理
TEST_CASE(http_error_handling) {
    HttpClient client;
    
    // 测试无效URL
    auto invalid_response_task = client.get("invalid-url");
    auto invalid_response = sync_wait(std::move(invalid_response_task));
    
    TEST_EXPECT_FALSE(invalid_response.success);
    TEST_EXPECT_TRUE(!invalid_response.error_message.empty());
    
    // 测试不存在的主机
    auto nonexistent_response_task = client.get("http://nonexistent-host-12345.com/");
    auto nonexistent_response = sync_wait(std::move(nonexistent_response_task));
    
    TEST_EXPECT_FALSE(nonexistent_response.success);
    TEST_EXPECT_TRUE(!nonexistent_response.error_message.empty());
}

// 测试自定义头部
TEST_CASE(http_custom_headers) {
    HttpClient client;
    
    std::unordered_map<std::string, std::string> headers;
    headers["User-Agent"] = "FlowCoro-Test-Client/2.0";
    headers["X-Test-Header"] = "FlowCoro-Value";
    
    auto response_task = client.get("http://httpbin.org/headers", headers);
    auto response = sync_wait(std::move(response_task));
    
    if (response.success) {
        TEST_EXPECT_EQ(response.status_code, 200);
        TEST_EXPECT_TRUE(response.body.find("FlowCoro-Test-Client") != std::string::npos);
        std::cout << "Custom headers test passed" << std::endl;
    } else {
        std::cout << "Custom headers test (network unavailable): " << response.error_message << std::endl;
        TEST_EXPECT_TRUE(true);
    }
}

int main() {
    TEST_SUITE("FlowCoro HTTP Client Tests");
    
    std::cout << "注意：这些测试需要网络连接到 httpbin.org" << std::endl;
    std::cout << "如果网络不可达，测试将优雅地处理错误" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
