// HttpServer façade tests (localhost). CI-safe.
#include "flowcoro.hpp"
#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace flowcoro;
using namespace flowcoro::net;
using namespace flowcoro::test;

namespace {

Task<HttpResponse> health_handler(const HttpIncomingRequest&) {
    HttpResponse r;
    r.status_code = 200;
    r.status_text = "OK";
    r.body = "ok";
    r.success = true;
    co_return r;
}

Task<HttpResponse> echo_post_handler(const HttpIncomingRequest& req) {
    HttpResponse r;
    r.status_code = 200;
    r.status_text = "OK";
    r.body = req.body;
    r.success = true;
    co_return r;
}

Task<HttpResponse> echo_query_handler(const HttpIncomingRequest& req) {
    HttpResponse r;
    r.status_code = 200;
    r.status_text = "OK";
    r.body = req.query;
    r.success = true;
    co_return r;
}

struct ServerFixture {
    EventLoop* loop;
    std::unique_ptr<HttpServer> server;
    uint16_t port{0};

    ServerFixture() {
        loop = &GlobalEventLoop::get();
        server = std::make_unique<HttpServer>(loop);
        server->get("/health", health_handler);
        server->post("/echo", echo_post_handler);
        server->get("/echo", echo_query_handler);
        sync_wait(server->listen("127.0.0.1", 0));
        port = server->local_port();
        // Give accept_loop a beat to arm.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ~ServerFixture() {
        if (server) server->stop();
    }

    std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port) + path;
    }
};

void test_get_health() {
    std::cout << "\n=== test_get_health ===\n";
    ServerFixture fx;
    TEST_EXPECT_TRUE(fx.port > 0);
    TEST_EXPECT_TRUE(fx.server->is_running());

    HttpClient client;
    auto resp = client.request_sync(HttpMethod::GET, fx.url("/health"));
    TEST_EXPECT_TRUE(resp.success);
    TEST_EXPECT_EQ(resp.status_code, 200);
    TEST_EXPECT_TRUE(resp.body == "ok");
}

void test_post_echo_body() {
    std::cout << "\n=== test_post_echo_body ===\n";
    ServerFixture fx;
    HttpClient client;
    auto resp = client.request_sync(HttpMethod::POST, fx.url("/echo"),
                                    {{"Content-Type", "text/plain"}}, "ping-pong");
    TEST_EXPECT_TRUE(resp.success);
    TEST_EXPECT_EQ(resp.status_code, 200);
    TEST_EXPECT_TRUE(resp.body == "ping-pong");
}

void test_get_query_and_404() {
    std::cout << "\n=== test_get_query_and_404 ===\n";
    ServerFixture fx;
    HttpClient client;

    auto ok = client.request_sync(HttpMethod::GET, fx.url("/echo?hello=world"));
    TEST_EXPECT_TRUE(ok.success);
    TEST_EXPECT_EQ(ok.status_code, 200);
    TEST_EXPECT_TRUE(ok.body.find("hello=world") != std::string::npos);

    auto miss = client.request_sync(HttpMethod::GET, fx.url("/nope"));
    TEST_EXPECT_EQ(miss.status_code, 404);
    TEST_EXPECT_FALSE(miss.success);
}

void test_clean_shutdown() {
    std::cout << "\n=== test_clean_shutdown ===\n";
    auto& loop = GlobalEventLoop::get();
    HttpServer server(&loop);
    server.get("/health", health_handler);
    sync_wait(server.listen("127.0.0.1", 0));
    TEST_EXPECT_TRUE(server.is_running());
    TEST_EXPECT_TRUE(server.local_port() > 0);
    server.stop();
    TEST_EXPECT_FALSE(server.is_running());
}

}  // namespace

int main() {
    TestRunner::reset();
    try {
        test_get_health();
        test_post_echo_body();
        test_get_query_and_404();
        test_clean_shutdown();
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        TEST_EXPECT_TRUE(false);
    }
    GlobalEventLoop::shutdown();
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
