// Localhost echo smoke: FlowCoro net stack (Socket accept/connect/read/write).
// CI-safe: few round-trips, loose timeout. Not a throughput claim.
//
// 协程必须是自由函数：带捕获的临时 lambda 在表达式结束时销毁，帧里 this 悬垂。

#include "flowcoro.hpp"
#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <netinet/tcp.h>
#include <signal.h>
#endif

using namespace flowcoro;
using namespace flowcoro::net;

namespace {

void set_tcp_nodelay(socket_t fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
}

uint16_t bound_port(socket_t fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

Task<ssize_t> read_full(Socket& sock, char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = co_await sock.read(buf + got, n - got);
        if (r <= 0) co_return static_cast<ssize_t>(got);
        got += static_cast<size_t>(r);
    }
    co_return static_cast<ssize_t>(got);
}

Task<ssize_t> write_full(Socket& sock, const char* buf, size_t n) {
    size_t put = 0;
    while (put < n) {
        ssize_t w = co_await sock.write(buf + put, n - put);
        if (w <= 0) co_return static_cast<ssize_t>(put);
        put += static_cast<size_t>(w);
    }
    co_return static_cast<ssize_t>(put);
}

Task<void> echo_session(std::unique_ptr<Socket> client, std::atomic<int>& served,
                        int msgs, size_t payload) {
    std::vector<char> buf(payload);
    try {
        set_tcp_nodelay(client->fd());
        for (int i = 0; i < msgs; ++i) {
            ssize_t n = co_await read_full(*client, buf.data(), payload);
            if (n != static_cast<ssize_t>(payload)) break;
            ssize_t w = co_await write_full(*client, buf.data(), payload);
            if (w != static_cast<ssize_t>(payload)) break;
            served.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (...) {
    }
    client->close();
    co_return;
}

Task<void> accept_one(Socket& listen, std::shared_ptr<Task<void>>& session_hold,
                      std::atomic<int>& served, int msgs, size_t payload) {
    try {
        auto client = co_await listen.accept();
        session_hold = std::make_shared<Task<void>>(
            echo_session(std::move(client), served, msgs, payload));
    } catch (...) {
    }
    co_return;
}

Task<void> echo_client(EventLoop* loop, uint16_t port, std::atomic<int>& ok,
                       int msgs, size_t payload) {
    try {
        Socket sock(loop);
        co_await sock.connect("127.0.0.1", port);
        set_tcp_nodelay(sock.fd());
        std::vector<char> send_buf(payload, 'E');
        std::vector<char> recv_buf(payload, 0);
        for (int i = 0; i < msgs; ++i) {
            ssize_t w = co_await write_full(sock, send_buf.data(), payload);
            ssize_t r = co_await read_full(sock, recv_buf.data(), payload);
            if (w == static_cast<ssize_t>(payload) &&
                r == static_cast<ssize_t>(payload) &&
                std::memcmp(send_buf.data(), recv_buf.data(), payload) == 0) {
                ok.fetch_add(1, std::memory_order_relaxed);
            }
        }
        sock.close();
    } catch (const std::exception& e) {
        std::cerr << "echo client: " << e.what() << "\n";
    }
    co_return;
}

} // namespace

TEST_CASE(real_net_localhost_echo_roundtrip) {
#ifndef _WIN32
    ::signal(SIGPIPE, SIG_IGN);
#endif
    GlobalLogger::get().set_level(LogLevel::LOG_ERROR);

    auto& loop = GlobalEventLoop::get();
    auto& manager = CoroutineManager::get_instance();
    std::atomic<bool> stop_drive{false};
    std::thread driver([&] {
        while (!stop_drive.load(std::memory_order_acquire)) {
            manager.drive();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    constexpr size_t kPayload = 32;
    constexpr int kMsgs = 16;

    Socket listen_sock(&loop);
    TEST_EXPECT_TRUE(listen_sock.bind("127.0.0.1", 0));
    TEST_EXPECT_TRUE(listen_sock.listen(16));
    const uint16_t port = bound_port(listen_sock.fd());
    TEST_EXPECT_TRUE(port != 0);

    std::atomic<int> served{0};
    std::shared_ptr<Task<void>> session_hold;
    auto accept_task = accept_one(listen_sock, session_hold, served, kMsgs, kPayload);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::atomic<int> ok{0};
    auto client_task = echo_client(&loop, port, ok, kMsgs, kPayload);
    try {
        client_task.get(std::chrono::seconds(10));
    } catch (const std::exception& e) {
        std::cerr << "echo client get: " << e.what() << "\n";
    }

    listen_sock.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    stop_drive.store(true, std::memory_order_release);
    driver.join();
    GlobalEventLoop::shutdown();

    std::cout << "localhost echo ok=" << ok.load()
              << " served=" << served.load()
              << " port=" << port << "\n";
    TEST_EXPECT_EQ(ok.load(), kMsgs);
    TEST_EXPECT_TRUE(served.load() >= kMsgs - 1);
}

int main() {
    TEST_SUITE("real localhost socket echo");
    flowcoro::test::TestRunner::print_summary();
    return flowcoro::test::TestRunner::all_passed() ? 0 : 1;
}
