#pragma once
//
// Low-overhead cycle-counter timestamp for measuring sub-microsecond compute paths. clock_gettime
// costs ~15-25 ns per call, which would dominate (and perturb) a ~100 ns hot path; rdtscp is a
// handful of cycles. The counter is runtime-calibrated to nanoseconds against CLOCK_MONOTONIC once
// (the invariant-TSC frequency is not the core frequency, so it must be measured, not assumed).
// This measures pure on-CPU work — no NIC / hardware timestamps involved.
//

#include <cstdint>
#include <ctime>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace abt::tsc {

namespace detail {

[[nodiscard]] inline std::uint64_t monoNs() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

}

// Read the counter. rdtscp waits for prior instructions to retire; the trailing lfence stops the
// read from floating past later instructions, so the bracketed region is measured tightly.
[[nodiscard]] inline std::uint64_t now() noexcept {
#if defined(__x86_64__)
    unsigned aux = 0;
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#else
    return detail::monoNs();
#endif
}

// Nanoseconds per counter tick, calibrated once against CLOCK_MONOTONIC. Each trial pairs a tick
// read with a ns read; if the thread is descheduled *between* that pair the endpoint is skewed and
// the ratio is wild, so we take the median over several trials and sanity-clamp the result (a real
// TSC sits at ~1-5 GHz => 0.2-1.0 ns/tick; anything outside [0.05, 5.0] is a bad sample). On the
// non-x86 fallback now() already returns ns, so the ratio converges to ~1.0 and toNs is identity.
[[nodiscard]] inline double nsPerTick() noexcept {
    static const double value = []() noexcept {
        constexpr int kTrials = 5;
        double ratios[kTrials] = {};
        for (int trial = 0; trial < kTrials; ++trial) {
            const std::uint64_t c0 = now();
            const std::uint64_t t0 = detail::monoNs();
            std::uint64_t t = t0;
            while (t - t0 < 1'000'000ull) {   // ~1 ms spin
                t = detail::monoNs();
            }
            const std::uint64_t t1 = detail::monoNs();
            const std::uint64_t c1 = now();
            const std::uint64_t dc = c1 - c0;
            const std::uint64_t dt = t1 - t0;
            ratios[trial] = (dc == 0) ? 1.0 : static_cast<double>(dt) / static_cast<double>(dc);
        }
        for (int i = 1; i < kTrials; ++i) {
            const double key = ratios[i];
            int j = i - 1;
            while (j >= 0 && ratios[j] > key) {
                ratios[j + 1] = ratios[j];
                --j;
            }
            ratios[j + 1] = key;
        }
        double median = ratios[kTrials / 2];
        if (median < 0.05 || median > 5.0) {
            median = 1.0;
        }
        return median;
    }();
    return value;
}

// Force calibration up front so the first measured region never pays the 5 ms spin.
inline void warmUp() noexcept {
    (void)nsPerTick();
}

[[nodiscard]] inline std::uint64_t toNs(std::uint64_t ticks) noexcept {
    return static_cast<std::uint64_t>(static_cast<double>(ticks) * nsPerTick());
}

}
