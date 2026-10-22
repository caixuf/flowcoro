// App-facing HTTP client tests against a local blocking HTTP/1.1 stub.
// CI-safe: no external network (unlike the httpbin path in test_network.cpp).
// Does not touch core scheduler / thread_pool.

#include "flowcoro.hpp"
#include "test_framework.h"

#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace flowcoro;
using namespace flowcoro::net;
using namespace flowcoro::test;

namespace {

#ifndef _WIN32
class LocalHttpStub {
public:
    explicit LocalHttpStub(std::string body)
        : body_(std::move(body)), running_(true) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) throw std::runtime_error("socket failed");
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("bind failed");
        }
        if (::listen(listen_fd_, 8) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("listen failed");
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("getsockname failed");
        }
        port_ = ntohs(addr.sin_port);
        worker_ = std::thread([this] { serve_loop(); });
    }

    ~LocalHttpStub() {
        running_.store(false);
        int poke = ::socket(AF_INET, SOCK_STREAM, 0);
        if (poke >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_);
            ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            ::close(poke);
        }
        if (worker_.joinable()) worker_.join();
        if (listen_fd_ >= 0) ::close(listen_fd_);
    }

    uint16_t port() const { return port_; }

private:
    void serve_loop() {
        while (running_.load()) {
            int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) continue;
            if (!running_.load()) {
                ::close(client);
                break;
            }
            char buf[4096];
            (void)::recv(client, buf, sizeof(buf), 0);
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: " + std::to_string(body_.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + body_;
            (void)::send(client, resp.data(), resp.size(), 0);
            ::close(client);
        }
    }

    std::string body_;
    std::atomic<bool> running_;
    int listen_fd_{-1};
    uint16_t port_{0};
    std::thread worker_;
};
#endif

}  // namespace

TEST_CASE(http_response_struct_defaults) {
    HttpResponse r;
    TEST_EXPECT_EQ(r.status_code, 0);
    TEST_EXPECT_TRUE(!r.success);
    TEST_EXPECT_TRUE(r.body.empty());
}

TEST_CASE(http_client_rejects_invalid_url) {
    HttpClient client;
    auto r = client.request_sync(HttpMethod::GET, "not-a-url");
    TEST_EXPECT_TRUE(!r.success);
    TEST_EXPECT_TRUE(r.error_message.find("Invalid URL") != std::string::npos);
}

TEST_CASE(http_client_rejects_https) {
    HttpClient client;
    auto r = client.request_sync(HttpMethod::GET, "https://example.com/");
    TEST_EXPECT_TRUE(!r.success);
    TEST_EXPECT_TRUE(r.error_message.find("HTTPS") != std::string::npos);
}

TEST_CASE(http_client_connect_refused) {
    HttpClient client;
    auto r = client.request_sync(HttpMethod::GET, "http://127.0.0.1:1/");
    TEST_EXPECT_TRUE(!r.success);
    TEST_EXPECT_TRUE(!r.error_message.empty());
}

#ifndef _WIN32
TEST_CASE(http_client_get_local_stub) {
    LocalHttpStub stub("flowcoro-app-facing-ok");
    HttpClient client;
    const std::string url =
        "http://127.0.0.1:" + std::to_string(stub.port()) + "/health";
    auto r = sync_wait(client.get(url));
    TEST_EXPECT_TRUE(r.success);
    TEST_EXPECT_EQ(r.status_code, 200);
    TEST_EXPECT_EQ(r.body, std::string("flowcoro-app-facing-ok"));
}

TEST_CASE(http_client_post_local_stub) {
    LocalHttpStub stub("posted");
    HttpClient client;
    const std::string url =
        "http://127.0.0.1:" + std::to_string(stub.port()) + "/echo";
    auto r = client.request_sync(HttpMethod::POST, url, {}, "{\"x\":1}");
    TEST_EXPECT_TRUE(r.success);
    TEST_EXPECT_EQ(r.status_code, 200);
    TEST_EXPECT_EQ(r.body, std::string("posted"));
}
#endif

int main() {
    TEST_SUITE("FlowCoro HTTP Client (app-facing, local stub)");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
