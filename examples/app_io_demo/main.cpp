/**
 * App-facing IO demo: HttpServer + Channel pipeline + when_all.
 *
 * Patterns: examples/echo_server.cpp, examples/chat_server.cpp,
 *           examples/hello_world_concurrent.cpp (when_all).
 * Primitives: make_channel, when_all, sync_wait, sleep_for.
 *
 * Build: cmake --build build --target app_io_demo
 * Run:   ./build/examples/app_io_demo
 */
#include <flowcoro.hpp>
#include <flowcoro/app.hpp>

#include <iostream>

using namespace flowcoro;
using namespace flowcoro::net;
using namespace flowcoro::app;

namespace {

Task<int> produce(std::shared_ptr<Channel<std::string>> ch, int n) {
    for (int i = 0; i < n; ++i) {
        co_await ch->send("job-" + std::to_string(i));
        co_await sleep_for(std::chrono::milliseconds(1));  // real wait, not CPU spin
    }
    ch->close();
    co_return n;
}

Task<int> consume(std::shared_ptr<Channel<std::string>> ch) {
    int done = 0;
    while (true) {
        auto v = co_await ch->recv();
        if (!v.has_value()) break;
        ++done;
    }
    co_return done;
}

}  // namespace

int main() {
    Stopwatch sw;
    auto& loop = GlobalEventLoop::get();
    HttpServer server(&loop);

    server.get("/health", [](const HttpIncomingRequest&) -> Task<HttpResponse> {
        co_return HttpResponse{200, "OK", "ok"};
    });

    sync_wait(server.listen("127.0.0.1", 0));
    const auto port = server.local_port();

    auto ch = make_pipeline_channel<std::string>(16);

    // Channel pipeline under when_all (fan_in recipe) — Task<int> like hello_world_concurrent
    auto [n_prod, n_cons] = sync_wait(fan_in(produce(ch, 8), consume(ch)));

    HttpClient client;
    auto resp = client.request_sync(
        HttpMethod::GET, "http://127.0.0.1:" + std::to_string(port) + "/health");

    std::cout << "app_io_demo\n"
              << "  http /health -> " << resp.status_code << " body=" << resp.body << "\n"
              << "  channel produced=" << n_prod << " consumed=" << n_cons << "\n"
              << "  wall_ms=" << sw.elapsed_ms()
              << " (honest wall clock, not a speed claim)\n";

    server.stop();
    GlobalEventLoop::shutdown();

    if (!resp.success || resp.status_code != 200 || n_prod != 8 || n_cons != 8) {
        std::cerr << "app_io_demo FAILED\n";
        return 1;
    }
    std::cout << "app_io_demo OK\n";
    return 0;
}
