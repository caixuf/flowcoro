#pragma once

// 轻量分位数统计，仅供 benchmarks/ 使用。
// 不放进 include/flowcoro：避免与 PR #20 的 rt_jitter.h 抢同一公共头。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace flowcoro::bench {

struct PercentileNs {
    std::size_t n = 0;
    double min_ns = 0;
    double p50_ns = 0;
    double p95_ns = 0;
    double p99_ns = 0;
    double max_ns = 0;
    double mean_ns = 0;
};

inline double pick_sorted(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0;
    const double idx = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

inline PercentileNs percentiles_ns(std::vector<double> samples) {
    PercentileNs r;
    r.n = samples.size();
    if (samples.empty()) return r;
    std::sort(samples.begin(), samples.end());
    r.min_ns = samples.front();
    r.max_ns = samples.back();
    r.p50_ns = pick_sorted(samples, 0.50);
    r.p95_ns = pick_sorted(samples, 0.95);
    r.p99_ns = pick_sorted(samples, 0.99);
    r.mean_ns = std::accumulate(samples.begin(), samples.end(), 0.0)
                / static_cast<double>(samples.size());
    return r;
}

inline PercentileNs percentiles_from_ns(
        const std::vector<std::chrono::nanoseconds>& samples) {
    std::vector<double> v;
    v.reserve(samples.size());
    for (auto s : samples) v.push_back(static_cast<double>(s.count()));
    return percentiles_ns(std::move(v));
}

inline double ns_to_us(double ns) { return ns / 1e3; }
inline double ns_to_ms(double ns) { return ns / 1e6; }

} // namespace flowcoro::bench
