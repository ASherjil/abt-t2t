#pragma once
//
// Clock.hpp -- wall-clock helpers for the (non-latency-critical) exchange simulator.
//
// nsSinceMidnightUtc() produces the nanoseconds-since-midnight value that ITCH/OUCH
// Timestamp fields expect. monotonicNs() is for scheduling the flow generator. The DUT's
// latency-critical path uses NIC hardware timestamps instead; these are for the sim.
//
#include <cstdint>
#include <ctime>

namespace abt::util {

inline std::uint64_t monotonicNs() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

inline std::uint64_t nsSinceMidnightUtc() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    const std::uint64_t secOfDay = static_cast<std::uint64_t>(ts.tv_sec) % 86'400ull;
    return secOfDay * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

}  // namespace abt::util
