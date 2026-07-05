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

// Nanoseconds per counter tick, calibrated once against a ~5 ms CLOCK_MONOTONIC window. On the
// non-x86 fallback now() already returns ns, so the ratio converges to ~1.0 and toNs is identity.
[[nodiscard]] inline double nsPerTick() noexcept {
    static const double value = []() noexcept {
        const std::uint64_t startNs = detail::monoNs();
        const std::uint64_t startTick = now();
        std::uint64_t nowNs = startNs;
        while (nowNs - startNs < 5'000'000ull) {
            nowNs = detail::monoNs();
        }
        const std::uint64_t endTick = now();
        const double elapsedNs = static_cast<double>(nowNs - startNs);
        const double ticks = static_cast<double>(endTick - startTick);
        if (ticks <= 0.0) {
            return 1.0;
        }
        return elapsedNs / ticks;
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
