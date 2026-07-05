//
// Tick-to-trade latency recorder — definitions.
//

#include "abt/dut/T2tRecorder.hpp"

#include <algorithm>

#include <fmt/core.h>

namespace abt::dut {

T2tRecorder::T2tRecorder(std::size_t capacity)
    : m_capacity(capacity == 0 ? 1 : capacity) {
    m_samples.reserve(m_capacity < (1u << 16) ? m_capacity : (1u << 16));
}

void T2tRecorder::record(std::uint64_t latencyNs) {
    if (m_count == 0) {
        m_min = latencyNs;
        m_max = latencyNs;
    } else {
        if (latencyNs < m_min) {
            m_min = latencyNs;
        }
        if (latencyNs > m_max) {
            m_max = latencyNs;
        }
    }
    ++m_count;
    if (m_samples.size() < m_capacity) {
        m_samples.push_back(latencyNs);
    }
}

std::size_t T2tRecorder::count() const noexcept {
    return m_count;
}

std::uint64_t T2tRecorder::min() const noexcept {
    return m_min;
}

std::uint64_t T2tRecorder::max() const noexcept {
    return m_max;
}

std::uint64_t T2tRecorder::percentile(double p) const {
    if (m_samples.empty()) {
        return 0;
    }
    std::vector<std::uint64_t> sorted(m_samples);
    std::sort(sorted.begin(), sorted.end());
    const double clamped = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    std::size_t idx = static_cast<std::size_t>(clamped * static_cast<double>(sorted.size() - 1)
                                               + 0.5);
    if (idx >= sorted.size()) {
        idx = sorted.size() - 1;
    }
    return sorted[idx];
}

void T2tRecorder::summary(std::string_view label) const {
    fmt::print("[{}] t2t ns: n={} stored={} min={} p50={} p90={} p99={} p99.9={} max={}\n",
               label, m_count, m_samples.size(), min(), percentile(0.50), percentile(0.90),
               percentile(0.99), percentile(0.999), max());
}

void T2tRecorder::clear() noexcept {
    m_samples.clear();
    m_count = 0;
    m_min = 0;
    m_max = 0;
}

}
