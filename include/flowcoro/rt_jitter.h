/**
 * @file rt_jitter.h
 * @brief Percentile helpers for flowcoro::rt latency / jitter samples.
 *
 * Not a runtime monitor: callers record tick-to-tick or deadline-tardiness
 * durations themselves, then fold them here. No production SLO is implied.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flowcoro::rt {

struct JitterReport {
    std::size_t n = 0;
    std::chrono::nanoseconds p50{0};
    std::chrono::nanoseconds p99{0};
    std::chrono::nanoseconds max{0};
    std::chrono::nanoseconds mean{0};
};

/// Sort a copy of `samples` and report p50 / p99 / max / mean.
/// pK uses index `(K/100) * (n-1)` (nearest-rank, no interpolation).
inline JitterReport jitter_report(std::vector<std::chrono::nanoseconds> samples) {
    JitterReport r;
    r.n = samples.size();
    if (r.n == 0) return r;

    std::sort(samples.begin(), samples.end());
    r.max = samples.back();
    const auto at_pct = [&](int pct) {
        const std::size_t idx =
            (static_cast<std::size_t>(pct) * (r.n - 1)) / 100;
        return samples[idx];
    };
    r.p50 = at_pct(50);
    r.p99 = at_pct(99);

    std::int64_t acc = 0;
    for (auto s : samples) acc += s.count();
    r.mean = std::chrono::nanoseconds{acc / static_cast<std::int64_t>(r.n)};
    return r;
}

inline double ns_to_ms(std::chrono::nanoseconds ns) noexcept {
    return static_cast<double>(ns.count()) / 1'000'000.0;
}

} // namespace flowcoro::rt
