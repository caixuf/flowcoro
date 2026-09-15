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
#include <mutex>
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

Task<void> persist_session(std::unique_ptr<Socket> client, std::atomic<bool>& running,
                           std::atomic<int>& served, size_t payload) {
    std::vector<char> buf(payload);
    try {
        set_tcp_nodelay(client->fd());
        while (running.load(std::memory_order_acquire)) {
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

Task<void> persist_accept_loop(Socket& listen, std::atomic<bool>& running,
                               std::vector<std::shared_ptr<Task<void>>>& sessions,
                               std::mutex& sessions_mu, std::atomic<int>& served,
                               size_t payload) {
    while (running.load(std::memory_order_acquire)) {
        try {
            auto client = co_await listen.accept();
            if (!client) continue;
            auto task = std::make_shared<Task<void>>(
                persist_session(std::move(client), running, served, payload));
            std::lock_guard<std::mutex> lk(sessions_mu);
            sessions.push_back(std::move(task));
        } catch (...) {
            if (!running.load(std::memory_order_acquire)) break;
        }
    }
    co_return;
}

Task<int> persist_client(EventLoop* loop, uint16_t port, size_t payload,
                         std::chrono::steady_clock::time_point deadline) {
    int ok = 0;
    try {
        Socket sock(loop);
        co_await sock.connect("127.0.0.1", port);
        set_tcp_nodelay(sock.fd());
        std::vector<char> send_buf(payload, 'P');
        std::vector<char> recv_buf(payload, 0);
        while (std::chrono::steady_clock::now() < deadline) {
            ssize_t w = co_await write_full(sock, send_buf.data(), payload);
            ssize_t r = co_await read_full(sock, recv_buf.data(), payload);
            if (w != static_cast<ssize_t>(payload) ||
                r != static_cast<ssize_t>(payload)) {
                break;
            }
            ++ok;
        }
        sock.close();
    } catch (...) {
    }
    co_return ok;
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

// CI-safe persistent burst: several clients, hundreds of round-trips, no timeout.
// Uses a local EventLoop so it does not depend on GlobalEventLoop (the other
// case shuts that singleton down).
TEST_CASE(real_net_persistent_echo_no_timeout) {
#ifndef _WIN32
    ::signal(SIGPIPE, SIG_IGN);
#endif
    GlobalLogger::get().set_level(LogLevel::LOG_ERROR);

    EventLoop loop;
    loop.start();
    auto& manager = CoroutineManager::get_instance();
    std::atomic<bool> stop_drive{false};
    std::thread driver([&] {
        while (!stop_drive.load(std::memory_order_acquire)) {
            manager.drive();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    constexpr size_t kPayload = 64;
    constexpr int kClients = 4;
    constexpr int kDurationMs = 300;

    Socket listen_sock(&loop);
    TEST_EXPECT_TRUE(listen_sock.bind("127.0.0.1", 0));
    TEST_EXPECT_TRUE(listen_sock.listen(16));
    const uint16_t port = bound_port(listen_sock.fd());
    TEST_EXPECT_TRUE(port != 0);

    std::atomic<bool> running{true};
    std::atomic<int> served{0};
    std::mutex sessions_mu;
    std::vector<std::shared_ptr<Task<void>>> sessions;
    auto accept_task = persist_accept_loop(listen_sock, running, sessions,
                                           sessions_mu, served, kPayload);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kDurationMs);
    std::vector<Task<int>> clients;
    clients.reserve(static_cast<size_t>(kClients));
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back(persist_client(&loop, port, kPayload, deadline));
    }

    int ok_sum = 0;
    int timeouts = 0;
    for (auto& t : clients) {
        try {
            ok_sum += t.get(std::chrono::seconds(8));
        } catch (const std::exception& e) {
            std::cerr << "persistent client get: " << e.what() << "\n";
            ++timeouts;
        }
    }

    running.store(false, std::memory_order_release);
    listen_sock.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    stop_drive.store(true, std::memory_order_release);
    driver.join();
    loop.stop();
    loop.wait_for_stop();

    std::cout << "persistent echo ok=" << ok_sum
              << " served=" << served.load()
              << " timeouts=" << timeouts
              << " port=" << port << "\n";
    TEST_EXPECT_EQ(timeouts, 0);
    TEST_EXPECT_TRUE(ok_sum > 0);
}

int main() {
    TEST_SUITE("real localhost socket echo");
    flowcoro::test::TestRunner::print_summary();
    return flowcoro::test::TestRunner::all_passed() ? 0 : 1;
}
