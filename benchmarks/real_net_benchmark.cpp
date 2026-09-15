/**
 * FlowCoro 真实套接字 IO 基准（localhost 回环，不是网卡/线缆）。
 *
 * 走 flowcoro::net：EventLoop (Linux epoll / Windows WSAPoll) +
 * Socket::accept/connect/read/write。不要把 professional_flowcoro_benchmark
 * 里的 Echo/HTTP 行当成 QPS —— 那些是 CPU-sim，没有 socket。
 *
 * 用法:
 *   ./real_net_benchmark
 *   ./real_net_benchmark echo --duration-ms 2000 --clients 8 --payload 64
 *   ./real_net_benchmark connect --connections 200 --payload 64
 *
 * 环境变量: FLOWCORO_REAL_NET_DURATION_MS / CLIENTS / PAYLOAD / CONNECTIONS / PORT
 */

#include "flowcoro.hpp"
#include "bench_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <netinet/tcp.h>
#include <signal.h>
#endif

using namespace flowcoro;
using namespace flowcoro::net;
using steady = std::chrono::steady_clock;

namespace {

struct Options {
    std::string mode = "all";
    int duration_ms = 1500;
    int clients = 8;
    int payload = 64;
    int connections = 200;
    uint16_t port = 0;  // 0 = 内核分配
};

int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

void apply_env(Options& o) {
    o.duration_ms = env_int("FLOWCORO_REAL_NET_DURATION_MS", o.duration_ms);
    o.clients = env_int("FLOWCORO_REAL_NET_CLIENTS", o.clients);
    o.payload = env_int("FLOWCORO_REAL_NET_PAYLOAD", o.payload);
    o.connections = env_int("FLOWCORO_REAL_NET_CONNECTIONS", o.connections);
    o.port = static_cast<uint16_t>(env_int("FLOWCORO_REAL_NET_PORT", o.port));
}

bool parse_args(int argc, char** argv, Options& o) {
    apply_env(o);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](int& dst) {
            if (i + 1 >= argc) return false;
            dst = std::stoi(argv[++i]);
            return true;
        };
        if (a == "echo" || a == "connect" || a == "all") {
            o.mode = a;
        } else if (a == "--duration-ms") {
            if (!need(o.duration_ms)) return false;
        } else if (a == "--clients") {
            if (!need(o.clients)) return false;
        } else if (a == "--payload") {
            if (!need(o.payload)) return false;
        } else if (a == "--connections") {
            if (!need(o.connections)) return false;
        } else if (a == "--port") {
            int p = 0;
            if (!need(p)) return false;
            o.port = static_cast<uint16_t>(p);
        } else if (a == "-h" || a == "--help") {
            return false;
        } else {
            std::cerr << "unknown arg: " << a << "\n";
            return false;
        }
    }
    if (o.clients < 1) o.clients = 1;
    if (o.payload < 1) o.payload = 1;
    if (o.duration_ms < 50) o.duration_ms = 50;
    if (o.connections < 1) o.connections = 1;
    return true;
}

void print_usage() {
    std::cout
        << "Usage: real_net_benchmark [echo|connect|all] [options]\n"
        << "  --duration-ms N   persistent-echo wall time (default 1500)\n"
        << "  --clients N       concurrent echo clients (default 8)\n"
        << "  --payload N       echo payload bytes (default 64)\n"
        << "  --connections N   connect-rate samples (default 200)\n"
        << "  --port N          listen port, 0 = ephemeral (default 0)\n"
        << "\nThis is localhost loopback. It is NOT NIC/wire throughput,\n"
        << "and it is NOT the CPU-sim Echo/HTTP rows in professional_flowcoro_benchmark.\n";
}

void set_tcp_nodelay(socket_t fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
}

uint16_t bound_port(socket_t fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

struct ClientStats {
    uint64_t ok = 0;
    uint64_t fail = 0;
    std::vector<double> rtt_ns;
};

void merge_stats(ClientStats& dst, ClientStats&& src) {
    dst.ok += src.ok;
    dst.fail += src.fail;
    dst.rtt_ns.insert(dst.rtt_ns.end(), src.rtt_ns.begin(), src.rtt_ns.end());
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

struct EchoServer {
    EventLoop* loop = nullptr;
    std::unique_ptr<Socket> listen_sock;
    std::optional<Task<void>> accept_task;
    std::mutex sessions_mu;
    std::vector<std::shared_ptr<Task<void>>> sessions;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> accepts{0};
    std::atomic<uint64_t> echoes{0};
    std::atomic<uint64_t> session_errors{0};
    size_t payload = 64;
    uint16_t port = 0;

    Task<void> session(std::unique_ptr<Socket> sock) {
        std::vector<char> buf(payload);
        try {
            set_tcp_nodelay(sock->fd());
            while (running.load(std::memory_order_acquire)) {
                ssize_t n = co_await read_full(*sock, buf.data(), payload);
                if (n != static_cast<ssize_t>(payload)) break;
                ssize_t w = co_await write_full(*sock, buf.data(), payload);
                if (w != static_cast<ssize_t>(payload)) break;
                echoes.fetch_add(1, std::memory_order_relaxed);
            }
        } catch (...) {
            session_errors.fetch_add(1, std::memory_order_relaxed);
        }
        sock->close();
        co_return;
    }

    Task<void> accept_loop() {
        while (running.load(std::memory_order_acquire)) {
            try {
                auto client = co_await listen_sock->accept();
                if (!client) continue;
                accepts.fetch_add(1, std::memory_order_relaxed);
                auto task = std::make_shared<Task<void>>(session(std::move(client)));
                std::lock_guard<std::mutex> lk(sessions_mu);
                sessions.push_back(std::move(task));
                auto it = sessions.begin();
                while (it != sessions.end()) {
                    if (!*it || (*it)->is_settled()) it = sessions.erase(it);
                    else ++it;
                }
            } catch (...) {
                if (!running.load(std::memory_order_acquire)) break;
            }
        }
        co_return;
    }

    bool start(uint16_t requested_port) {
        listen_sock = std::make_unique<Socket>(loop);
        if (!listen_sock->bind("127.0.0.1", requested_port)) {
            std::cerr << "bind 127.0.0.1:" << requested_port << " failed\n";
            return false;
        }
        if (!listen_sock->listen(512)) {
            std::cerr << "listen failed\n";
            return false;
        }
        port = bound_port(listen_sock->fd());
        if (port == 0) {
            std::cerr << "getsockname failed\n";
            return false;
        }
        running.store(true, std::memory_order_release);
        accept_task = accept_loop();
        return true;
    }

    void stop() {
        running.store(false, std::memory_order_release);
        if (listen_sock) listen_sock->close();
    }
};

Task<ClientStats> persistent_client(EventLoop* loop, uint16_t port, size_t payload,
                                    steady::time_point deadline) {
    ClientStats st;
    st.rtt_ns.reserve(256);
    try {
        auto sock = std::make_unique<Socket>(loop);
        co_await sock->connect("127.0.0.1", port);
        set_tcp_nodelay(sock->fd());
        std::vector<char> send_buf(payload, 'A');
        std::vector<char> recv_buf(payload, 0);
        while (steady::now() < deadline) {
            const auto t0 = steady::now();
            ssize_t w = co_await write_full(*sock, send_buf.data(), payload);
            if (w != static_cast<ssize_t>(payload)) {
                st.fail++;
                break;
            }
            ssize_t r = co_await read_full(*sock, recv_buf.data(), payload);
            const auto t1 = steady::now();
            if (r != static_cast<ssize_t>(payload)) {
                st.fail++;
                break;
            }
            st.ok++;
            st.rtt_ns.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        }
        sock->close();
    } catch (...) {
        st.fail++;
    }
    co_return st;
}

Task<ClientStats> one_shot_client(EventLoop* loop, uint16_t port, size_t payload) {
    ClientStats st;
    try {
        auto sock = std::make_unique<Socket>(loop);
        const auto t0 = steady::now();
        co_await sock->connect("127.0.0.1", port);
        set_tcp_nodelay(sock->fd());
        std::vector<char> send_buf(payload, 'B');
        std::vector<char> recv_buf(payload, 0);
        ssize_t w = co_await write_full(*sock, send_buf.data(), payload);
        ssize_t r = 0;
        if (w == static_cast<ssize_t>(payload)) {
            r = co_await read_full(*sock, recv_buf.data(), payload);
        }
        const auto t1 = steady::now();
        sock->close();
        if (w == static_cast<ssize_t>(payload) && r == static_cast<ssize_t>(payload)) {
            st.ok = 1;
            st.rtt_ns.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        } else {
            st.fail = 1;
        }
    } catch (...) {
        st.fail = 1;
    }
    co_return st;
}

void print_pct(const char* label, const bench::PercentileNs& p) {
    std::cout << std::fixed << std::setprecision(1)
              << "  " << std::left << std::setw(22) << label
              << " n=" << p.n
              << "  p50=" << bench::ns_to_us(p.p50_ns) << " us"
              << "  p95=" << bench::ns_to_us(p.p95_ns) << " us"
              << "  p99=" << bench::ns_to_us(p.p99_ns) << " us"
              << "  max=" << bench::ns_to_us(p.max_ns) << " us"
              << "  mean=" << bench::ns_to_us(p.mean_ns) << " us\n";
}

struct NetRuntime {
    std::atomic<bool> stop{false};
    std::thread driver;
    EventLoop* loop = nullptr;

    void start() {
        GlobalLogger::get().set_level(LogLevel::LOG_ERROR);
        loop = &GlobalEventLoop::get();
        auto& mgr = CoroutineManager::get_instance();
        driver = std::thread([&mgr, this] {
            while (!stop.load(std::memory_order_acquire)) {
                mgr.drive();
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
    }

    void shutdown() {
        stop.store(true, std::memory_order_release);
        if (driver.joinable()) driver.join();
        GlobalEventLoop::shutdown();
    }
};

int run_echo(NetRuntime& rt, const Options& o) {
    EchoServer server;
    server.loop = rt.loop;
    server.payload = static_cast<size_t>(o.payload);
    if (!server.start(o.port)) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "\n=== persistent TCP echo (localhost loopback) ===\n"
              << "  bind=127.0.0.1:" << server.port
              << "  clients=" << o.clients
              << "  payload=" << o.payload << " B"
              << "  duration=" << o.duration_ms << " ms\n"
              << "  stack=flowcoro::net Socket + EventLoop (epoll/WSAPoll)\n"
              << "  label=NOT wire/NIC; NOT professional_flowcoro_benchmark Echo CPU-sim\n";

    const auto deadline = steady::now() + std::chrono::milliseconds(o.duration_ms);
    const auto t0 = steady::now();

    std::vector<Task<ClientStats>> tasks;
    tasks.reserve(static_cast<size_t>(o.clients));
    for (int i = 0; i < o.clients; ++i) {
        tasks.emplace_back(persistent_client(rt.loop, server.port,
                                             static_cast<size_t>(o.payload), deadline));
    }

    const auto wait = std::chrono::milliseconds(o.duration_ms + 15000);
    ClientStats all;
    for (auto& t : tasks) {
        try {
            merge_stats(all, t.get(wait));
        } catch (const std::exception& e) {
            std::cerr << "client get() failed: " << e.what() << "\n";
            all.fail++;
        }
    }
    const auto wall = steady::now() - t0;
    const double sec = std::chrono::duration<double>(wall).count();

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto pct = bench::percentiles_ns(all.rtt_ns);
    const double req_s = sec > 0 ? static_cast<double>(all.ok) / sec : 0;
    std::cout << std::fixed << std::setprecision(1)
              << "  wall=" << (sec * 1e3) << " ms"
              << "  ok_req=" << all.ok
              << "  fail=" << all.fail
              << "  req/s=" << req_s
              << "  accepts=" << server.accepts.load()
              << "  server_echoes=" << server.echoes.load() << "\n";
    print_pct("echo RTT", pct);
    if (all.ok == 0) {
        std::cerr << "  FAIL: zero successful echo requests\n";
        return 1;
    }
    return 0;
}

int run_connect(NetRuntime& rt, const Options& o) {
    EchoServer server;
    server.loop = rt.loop;
    server.payload = static_cast<size_t>(o.payload);
    if (!server.start(o.port)) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "\n=== connect + 1 echo + close (localhost loopback) ===\n"
              << "  bind=127.0.0.1:" << server.port
              << "  connections=" << o.connections
              << "  payload=" << o.payload << " B  wave=32\n"
              << "  label=connections/s includes handshake + one request/response\n";

    const auto t0 = steady::now();
    ClientStats all;
    const int wave = 32;
    int launched = 0;
    const auto wait = std::chrono::seconds(20);
    while (launched < o.connections) {
        const int n = std::min(wave, o.connections - launched);
        std::vector<Task<ClientStats>> tasks;
        tasks.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            tasks.emplace_back(one_shot_client(rt.loop, server.port,
                                               static_cast<size_t>(o.payload)));
        }
        for (auto& t : tasks) {
            try {
                merge_stats(all, t.get(wait));
            } catch (const std::exception& e) {
                std::cerr << "connect client get() failed: " << e.what() << "\n";
                all.fail++;
            }
        }
        launched += n;
    }
    const auto wall = steady::now() - t0;
    const double sec = std::chrono::duration<double>(wall).count();
    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto pct = bench::percentiles_ns(all.rtt_ns);
    const double conn_s = sec > 0 ? static_cast<double>(all.ok) / sec : 0;
    std::cout << std::fixed << std::setprecision(1)
              << "  wall=" << (sec * 1e3) << " ms"
              << "  ok=" << all.ok
              << "  fail=" << all.fail
              << "  conn/s=" << conn_s
              << "  accepts=" << server.accepts.load() << "\n";
    print_pct("connect+echo RTT", pct);
    if (all.ok == 0) {
        std::cerr << "  FAIL: zero successful connections\n";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    ::signal(SIGPIPE, SIG_IGN);
#endif

    Options opt;
    if (!parse_args(argc, argv, opt)) {
        print_usage();
        return 2;
    }

    std::cout
        << "FlowCoro REAL SOCKET IO benchmark\n"
        << "  version=" << FLOWCORO_VERSION_STRING
        << "  threads=" << std::thread::hardware_concurrency()
#ifdef NDEBUG
        << "  build=Release\n"
#else
        << "  build=Debug\n"
#endif
        << "  transport=TCP 127.0.0.1 (kernel loopback, not a physical NIC)\n"
        << "  distinction: professional_flowcoro_benchmark Echo/HTTP/Memory-Pool rows\n"
        << "               are CPU-sim / malloc; they are not this number.\n";

    NetRuntime rt;
    rt.start();
    int rc = 0;
    try {
        if (opt.mode == "echo" || opt.mode == "all") rc |= run_echo(rt, opt);
        if (opt.mode == "connect" || opt.mode == "all") rc |= run_connect(rt, opt);
    } catch (const std::exception& e) {
        std::cerr << "benchmark exception: " << e.what() << "\n";
        rc = 1;
    }
    rt.shutdown();

    std::cout
        << "\nGaps / how to read:\n"
        << "  - Loopback only. Do not quote as NIC or multi-host QPS.\n"
        << "  - Each Socket::read/write currently add_fd/remove_fd (one-shot epoll).\n"
        << "  - Not HTTP; fixed-size echo. Do not label this 'HTTP throughput'.\n"
        << "  - No comparison multiplier vs Go/Rust is printed; none was measured here.\n";
    return rc;
}
