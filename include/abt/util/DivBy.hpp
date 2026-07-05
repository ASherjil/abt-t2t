#pragma once
//
// Fast unsigned division by a runtime-constant divisor (the classic libdivide / Granlund-Montgomery
// trick). A hardware DIV is ~20+ cycle latency and sits on the critical path of the order book's
// price -> level-index mapping, which divides by the tick size on every add and every remove.
// Precompute a magic multiplier + shift once when the divisor is known, then each division becomes
// a high multiply and a shift. Correct for every uint32 dividend; power-of-two divisors collapse to
// a pure shift. Declarations first per project style; definitions at the bottom.
//

#include <cstdint>

namespace abt::util {

class DivBy {
public:
    DivBy() noexcept = default;
    explicit DivBy(std::uint32_t d) noexcept;

    [[nodiscard]] std::uint32_t operator()(std::uint32_t n) const noexcept;

private:
    std::uint32_t m_magic = 0;   // 0 => pure shift (power-of-two divisor, incl. d == 1)
    std::uint32_t m_shift = 0;
    bool          m_add   = false;
};

inline DivBy::DivBy(std::uint32_t d) noexcept {
    if ((d & (d - 1)) == 0) {
        // Power of two (including d == 1): division is a right shift by log2(d).
        m_magic = 0;
        m_shift = static_cast<std::uint32_t>(__builtin_ctz(d));
        m_add = false;
        return;
    }
    const std::uint32_t floorLog2 = 31u - static_cast<std::uint32_t>(__builtin_clz(d));
    const std::uint64_t twoToN = std::uint64_t{1} << (32u + floorLog2);
    std::uint32_t proposed = static_cast<std::uint32_t>(twoToN / d);
    const std::uint32_t rem = static_cast<std::uint32_t>(twoToN - static_cast<std::uint64_t>(proposed) * d);
    const std::uint32_t e = d - rem;
    if (e < (std::uint32_t{1} << floorLog2)) {
        m_shift = floorLog2;
        m_add = false;
    } else {
        proposed += proposed;
        const std::uint32_t rem2 = rem + rem;
        if (rem2 >= d || rem2 < rem) {
            proposed += 1;
        }
        m_shift = floorLog2;
        m_add = true;
    }
    m_magic = proposed + 1;
}

inline std::uint32_t DivBy::operator()(std::uint32_t n) const noexcept {
    if (m_magic == 0) {
        return n >> m_shift;
    }
    const std::uint32_t q =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(m_magic) * n) >> 32);
    if (m_add) {
        const std::uint32_t t = ((n - q) >> 1) + q;
        return t >> m_shift;
    }
    return q >> m_shift;
}

}
