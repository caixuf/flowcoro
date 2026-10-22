/**
 * Thin C++ mirror: N Tasks + ToolFanIn (when_all) + CancellationToken.
 * App-facing only — no sched core. See docs/AGENT_SCHED.md.
 *
 * Build: cmake --build build --target agent_tool_fanout_demo
 * Run:   ./build/examples/agent_tool_fanout_demo
 */
#include <flowcoro.hpp>
#include <flowcoro/app.hpp>

#include <chrono>
#include <iostream>
#include <tuple>

using namespace flowcoro;
using namespace flowcoro::app;
using namespace std::chrono_literals;

namespace {

Task<int> stub_tool(int id, int work_ms, CancellationToken token) {
    const auto step = 20ms;
    int waited = 0;
    while (waited < work_ms) {
        if (token.is_cancelled()) {
            co_return -id;  // cancelled marker (negative id)
        }
        co_await sleep_for(step);
        waited += 20;
    }
    co_return id;
}

Task<int> cancel_after_ms(CancellationTokenSource& src, int ms) {
    co_await sleep_for(std::chrono::milliseconds(ms));
    src.cancel();
    co_return 0;
}

}  // namespace

int main() {
    Stopwatch sw;
    CancellationTokenSource src;
    auto token = src.token();

    // Six stub tools + a helper that cancels after 150ms (short tools finish; long cancel).
    auto tup = sync_wait(tool_fan_in(
        stub_tool(0, 80, token),
        stub_tool(1, 120, token),
        stub_tool(2, 200, token),
        stub_tool(3, 300, token),
        stub_tool(4, 450, token),
        stub_tool(5, 600, token),
        cancel_after_ms(src, 150)));

    const int vals[6] = {
        std::get<0>(tup), std::get<1>(tup), std::get<2>(tup),
        std::get<3>(tup), std::get<4>(tup), std::get<5>(tup)};

    int ok = 0;
    int cancelled = 0;
    for (int v : vals) {
        if (v < 0) {
            ++cancelled;
            std::cout << "  tool " << -v << " cancelled\n";
        } else {
            ++ok;
            std::cout << "  tool " << v << " ok\n";
        }
    }

    std::cout << "agent_tool_fanout_demo\n"
              << "  ok=" << ok << " cancelled=" << cancelled << "\n"
              << "  wall_ms=" << sw.elapsed_ms()
              << " (honest wall clock, not a speed claim)\n";

    if (ok < 1 || cancelled < 1) {
        std::cerr << "agent_tool_fanout_demo FAILED (need both ok and cancelled)\n";
        return 1;
    }
    std::cout << "agent_tool_fanout_demo OK\n";
    return 0;
}
