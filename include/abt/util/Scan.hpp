#pragma once
//
// Find the nearest non-empty level in a dense uint32 array — the scan the order book runs when its
// best bid/ask level empties and it must fall back to the next populated price. The scalar version
// tests one level per step; the AVX2 version tests eight per instruction (vpcmpeqd + movemask),
// which pays off when the book has gaps and the fallback level is far away (a p99-tail case). Both
// are exposed so the test can cross-check them and the benchmark can compare; scan*NonZero() picks
// AVX2 when the target supports it. -march=x86-64-v3 (this project's build) implies AVX2.
//

#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace abt::util {

inline constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

// Largest index j in [0, start] with a[j] != 0, else kNoIndex.
[[nodiscard]] inline std::size_t scanDownNonZeroScalar(const std::uint32_t* a,
                                                       std::size_t start) noexcept {
    std::size_t i = start;
    for (;;) {
        if (a[i] != 0) {
            return i;
        }
        if (i == 0) {
            return kNoIndex;
        }
        --i;
    }
}

// Smallest index j in [start, last] with a[j] != 0, else kNoIndex.
[[nodiscard]] inline std::size_t scanUpNonZeroScalar(const std::uint32_t* a, std::size_t start,
                                                     std::size_t last) noexcept {
    for (std::size_t i = start; i <= last; ++i) {
        if (a[i] != 0) {
            return i;
        }
    }
    return kNoIndex;
}

#if defined(__AVX2__)

[[nodiscard]] inline std::size_t scanDownNonZeroAvx2(const std::uint32_t* a,
                                                     std::size_t start) noexcept {
    const __m256i zero = _mm256_setzero_si256();
    std::size_t i = start;
    while (i >= 7) {
        const std::size_t base = i - 7;
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + base));
        const int zmask = _mm256_movemask_ps(
            _mm256_castsi256_ps(_mm256_cmpeq_epi32(v, zero)));
        const unsigned nz = static_cast<unsigned>(~zmask) & 0xFFu;
        if (nz != 0) {
            const int lane = 31 - __builtin_clz(nz);   // highest non-zero lane
            return base + static_cast<std::size_t>(lane);
        }
        if (base == 0) {
            return kNoIndex;
        }
        i = base - 1;
    }
    for (;;) {
        if (a[i] != 0) {
            return i;
        }
        if (i == 0) {
            return kNoIndex;
        }
        --i;
    }
}

[[nodiscard]] inline std::size_t scanUpNonZeroAvx2(const std::uint32_t* a, std::size_t start,
                                                   std::size_t last) noexcept {
    const __m256i zero = _mm256_setzero_si256();
    std::size_t i = start;
    while (i + 7 <= last) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
        const int zmask = _mm256_movemask_ps(
            _mm256_castsi256_ps(_mm256_cmpeq_epi32(v, zero)));
        const unsigned nz = static_cast<unsigned>(~zmask) & 0xFFu;
        if (nz != 0) {
            const int lane = __builtin_ctz(nz);   // lowest non-zero lane
            return i + static_cast<std::size_t>(lane);
        }
        i += 8;
    }
    for (; i <= last; ++i) {
        if (a[i] != 0) {
            return i;
        }
    }
    return kNoIndex;
}

#endif

[[nodiscard]] inline std::size_t scanDownNonZero(const std::uint32_t* a, std::size_t start) noexcept {
#if defined(__AVX2__)
    return scanDownNonZeroAvx2(a, start);
#else
    return scanDownNonZeroScalar(a, start);
#endif
}

[[nodiscard]] inline std::size_t scanUpNonZero(const std::uint32_t* a, std::size_t start,
                                               std::size_t last) noexcept {
#if defined(__AVX2__)
    return scanUpNonZeroAvx2(a, start, last);
#else
    return scanUpNonZeroScalar(a, start, last);
#endif
}

}
