/**
 * @file app.hpp
 * @brief Thin app-facing façade — recipes over existing primitives.
 *
 * Prefer #include <flowcoro.hpp> in apps; this header is the documented
 * map for HTTP + Channel demos. Does not touch sched / thread_pool / rt.
 *
 * Cites: HttpClient (http_client.h), HttpServer (http_server.h),
 * Channel / make_channel (channel.h), when_all / sync_wait (core).
 * Patterns: examples/echo_server.cpp, examples/chat_server.cpp.
 */
#pragma once

#include "http_client.h"
#include "http_server.h"
#include "channel.h"
#include "core.h"
#include "cancellation.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace flowcoro::app {

using net::HttpClient;
using net::HttpServer;
using net::HttpIncomingRequest;
using net::HttpResponse;
using net::HttpMethod;

/** Named recipe: bounded Channel pipeline (producer → consumer). */
template <typename T>
inline auto make_pipeline_channel(size_t capacity = 64) {
    return make_channel<T>(capacity);
}

/**
 * Named recipe: fan-in — wait for all tasks (forwards to when_all).
 * Prefer this name in app demos so call sites document intent.
 */
template <typename... Tasks>
inline auto fan_in(Tasks&&... tasks) {
    return when_all(std::forward<Tasks>(tasks)...);
}

/** Wall-clock helper for honest demo timings (not a benchmark claim). */
class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    double elapsed_ms() const {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now() - start_).count();
    }
private:
    std::chrono::steady_clock::time_point start_;
};


/**
 * Named recipe: agent tool fan-in — wait for all tool Tasks (forwards to when_all).
 * Pair with CancellationToken / CancellationTokenSource for cooperative cancel.
 * Alias name documents agent-sched intent; no new framework.
 */
template <typename... Tasks>
inline auto tool_fan_in(Tasks&&... tasks) {
    return when_all(std::forward<Tasks>(tasks)...);
}

/** Optional spelling used in docs / contracts. */
template <typename... Tasks>
inline auto ToolFanIn(Tasks&&... tasks) {
    return tool_fan_in(std::forward<Tasks>(tasks)...);
}

}  // namespace flowcoro::app
