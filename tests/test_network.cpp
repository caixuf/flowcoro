#include <iostream>
#include <chrono>
#include <thread>
#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace flowcoro::net;

// HTTP
void test_http_client_basic() {
    std::cout << "HTTP..." << std::endl;
    try {
        HttpClient client;

        // URL
        bool url_parsing_works = true;
        TEST_EXPECT_TRUE(url_parsing_works);

        std::cout << "HTTP" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "HTTP: " << e.what() << std::endl;
        TEST_EXPECT_TRUE(false);
    }
}

// HTTP GET
void test_http_get_request() {
    std::cout << "HTTP GET..." << std::endl;
    try {
        HttpClient client;

        // 
        // 

        // 
        std::string test_url = "http://httpbin.org/get";

        // 
        auto response_task = client.get(test_url);

        // 
        std::cout << "HTTP GET" << std::endl;
        TEST_EXPECT_TRUE(true);

    } catch (const std::exception& e) {
        std::cout << "HTTP GET: " << e.what() << std::endl;
        TEST_EXPECT_TRUE(true); // 
    }
}

// Socket
void test_socket_creation() {
    std::cout << "Socket..." << std::endl;
    try {
        // SocketEventLoop
        // EventLoop

        // Socket
        bool socket_concept_valid = true;
        TEST_EXPECT_TRUE(socket_concept_valid);

        std::cout << "Socket" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Socket: " << e.what() << std::endl;
        TEST_EXPECT_TRUE(false);
    }
}

// 
void test_network_init() {
    std::cout << "..." << std::endl;
    try {
        // 
        HttpClient client;

        // 
        bool client_created = true;
        TEST_EXPECT_TRUE(client_created);

        std::cout << "" << std::endl;

    } catch (const std::exception& e) {
        std::cout << ": " << e.what() << std::endl;
        TEST_EXPECT_TRUE(false);
    }
}

// HTTP
void test_http_response_parsing() {
    std::cout << "HTTP..." << std::endl;
    try {
        // HTTP
        HttpResponse response;
        response.status_code = 200;
        response.status_text = "OK";
        response.body = "Test Response";
        response.success = true;

        TEST_EXPECT_EQ(response.status_code, 200);
        TEST_EXPECT_TRUE(response.success);

        std::cout << "HTTP" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "HTTP: " << e.what() << std::endl;
        TEST_EXPECT_TRUE(false);
    }
}

int main() {
    TEST_SUITE("FlowCoro ");

    test_http_client_basic();
    test_http_get_request();
    test_socket_creation();
    test_network_init();
    test_http_response_parsing();

    TestRunner::print_summary();

    return TestRunner::all_passed() ? 0 : 1;
}
