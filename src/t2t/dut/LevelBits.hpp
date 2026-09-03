#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <vector>

namespace abt::dut {

class LevelBits {
public:
    static constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

    explicit LevelBits(std::pmr::memory_resource* mr) noexcept : m_l0(mr), m_l1(mr) {
    }

    void reset(std::size_t ticks) {
        const std::size_t w0 = (ticks + 63) / 64;
        const std::size_t w1 = (w0 + 63) / 64;
        m_l0.assign(w0 == 0 ? 1 : w0, 0);
        m_l1.assign(w1 == 0 ? 1 : w1, 0);
    }

    void clearAll() noexcept {
        for (auto& w : m_l0) {
            w = 0;
        }
        for (auto& w : m_l1) {
            w = 0;
        }
    }

    void set(std::size_t i) noexcept {
        m_l0[i >> 6] |= std::uint64_t{1} << (i & 63);
        m_l1[i >> 12] |= std::uint64_t{1} << ((i >> 6) & 63);
    }

    void clear(std::size_t i) noexcept {
        const std::size_t w = i >> 6;
        m_l0[w] &= ~(std::uint64_t{1} << (i & 63));
        if (m_l0[w] == 0) {
            m_l1[w >> 6] &= ~(std::uint64_t{1} << (w & 63));
        }
    }

    [[nodiscard]] std::size_t prev(std::size_t i) const noexcept {
        std::size_t         w = i >> 6;
        const std::size_t   b = i & 63;
        const std::uint64_t m = m_l0[w] & (b == 63 ? ~std::uint64_t{0} : ((std::uint64_t{1} << (b + 1)) - 1));
        if (m != 0) {
            return (w << 6) + 63 - static_cast<std::size_t>(std::countl_zero(m));
        }
        std::size_t   w1 = w >> 6;
        std::uint64_t m1 = m_l1[w1] & ((std::uint64_t{1} << (w & 63)) - 1);
        for (;;) {
            if (m1 != 0) {
                const std::size_t ww = (w1 << 6) + 63 - static_cast<std::size_t>(std::countl_zero(m1));
                return (ww << 6) + 63 - static_cast<std::size_t>(std::countl_zero(m_l0[ww]));
            }
            if (w1 == 0) {
                return kNone;
            }
            m1 = m_l1[--w1];
        }
    }

    [[nodiscard]] std::size_t next(std::size_t i) const noexcept {
        const std::size_t   w = i >> 6;
        const std::size_t   b = i & 63;
        const std::uint64_t m = m_l0[w] & (~std::uint64_t{0} << b);
        if (m != 0) {
            return (w << 6) + static_cast<std::size_t>(std::countr_zero(m));
        }
        std::size_t         w1 = w >> 6;
        const std::size_t   b1 = w & 63;
        std::uint64_t       m1 = b1 == 63 ? 0 : (m_l1[w1] & (~std::uint64_t{0} << (b1 + 1)));
        for (;;) {
            if (m1 != 0) {
                const std::size_t ww = (w1 << 6) + static_cast<std::size_t>(std::countr_zero(m1));
                return (ww << 6) + static_cast<std::size_t>(std::countr_zero(m_l0[ww]));
            }
            if (++w1 >= m_l1.size()) {
                return kNone;
            }
            m1 = m_l1[w1];
        }
    }

    [[nodiscard]] std::size_t bytes() const noexcept {
        return (m_l0.capacity() + m_l1.capacity()) * sizeof(std::uint64_t);
    }

private:
    std::pmr::vector<std::uint64_t> m_l0;
    std::pmr::vector<std::uint64_t> m_l1;
};

}   // namespace abt::dut
