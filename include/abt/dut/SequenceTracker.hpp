#pragma once

#include <cstdint>

namespace abt::dut {

class SequenceTracker {
public:
    enum class Result : std::uint8_t { InOrder, Gap, Stale };

    Result onPacket(std::uint64_t seq, std::uint16_t count) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::uint64_t expected() const noexcept;
    [[nodiscard]] std::uint64_t gaps() const noexcept;
    [[nodiscard]] std::uint64_t missed() const noexcept;
    [[nodiscard]] std::uint64_t stale() const noexcept;

private:
    std::uint64_t m_expected = 0;
    std::uint64_t m_gaps     = 0;
    std::uint64_t m_missed   = 0;
    std::uint64_t m_stale    = 0;
    bool          m_started  = false;
};

}
