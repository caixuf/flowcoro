/**
 * App-facing demo: scheduler + HttpServer + Channel + clean shutdown.
 *
 * Build: cmake --build build --target http_channel_demo
 * Run:   ./build/examples/http_channel_demo [port]
 */
#include "flowcoro.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace flowcoro;
using namespace flowcoro::net;

static std::atomic<bool> g_running{true};

static void on_signal(int) {
    g_running.store(false, std::memory_order_release);
}

static Task<void> producer(std::shared_ptr<Channel<std::string>> ch, int n) {
    for (int i = 0; i < n; ++i) {
        co_await ch->send("job-" + std::to_string(i));
    }
    ch->close();
    co_return;
}

static Task<void> consumer(std::shared_ptr<Channel<std::string>> ch,
                           std::atomic<int>& done) {
    while (true) {
        auto v = co_await ch->recv();
        if (!v.has_value()) break;
        done.fetch_add(1, std::memory_order_relaxed);
    }
    co_return;
}

int main(int argc, char** argv) {
    uint16_t port = 18080;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto& loop = GlobalEventLoop::get();
    HttpServer server(&loop);

    auto jobs = make_channel<std::string>(32);
    std::atomic<int> consumed{0};

    // Hold producer/consumer tasks for the demo lifetime.
    auto prod = std::make_shared<Task<void>>(producer(jobs, 5));
    auto cons = std::make_shared<Task<void>>(consumer(jobs, consumed));

    server.get("/health", [](const HttpIncomingRequest&) -> Task<HttpResponse> {
        HttpResponse r;
        r.status_code = 200;
        r.status_text = "OK";
        r.body = "ok";
        r.headers["Content-Type"] = "text/plain";
        r.success = true;
        co_return r;
    });

    server.get("/echo", [](const HttpIncomingRequest& req) -> Task<HttpResponse> {
        HttpResponse r;
        r.status_code = 200;
        r.status_text = "OK";
        r.body = req.query.empty() ? "empty" : req.query;
        r.headers["Content-Type"] = "text/plain";
        r.success = true;
        co_return r;
    });

    server.post("/echo", [](const HttpIncomingRequest& req) -> Task<HttpResponse> {
        HttpResponse r;
        r.status_code = 200;
        r.status_text = "OK";
        r.body = req.body;
        r.headers["Content-Type"] = "text/plain";
        r.success = true;
        co_return r;
    });

    sync_wait(server.listen("127.0.0.1", port));
    port = server.local_port();
    std::cout << "http_channel_demo listening on http://127.0.0.1:" << port << "\n"
              << "  GET  /health\n"
              << "  GET  /echo?msg=hi\n"
              << "  POST /echo\n"
              << "Ctrl-C to stop.\n";

    // Optional client self-check
    {
        HttpClient client;
        auto resp = client.request_sync(HttpMethod::GET,
            "http://127.0.0.1:" + std::to_string(port) + "/health");
        std::cout << "self-check /health -> " << resp.status_code
                  << " body=" << resp.body << "\n";
    }

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "shutting down (channel consumed=" << consumed.load() << ")...\n";
    server.stop();
    GlobalEventLoop::shutdown();
    std::cout << "bye\n";
    return 0;
}
