#include "abt/dut/SequenceTracker.hpp"

#include "abt/protocol/MoldUdp64.hpp"

namespace abt::dut {

SequenceTracker::Result SequenceTracker::onPacket(std::uint64_t seq, std::uint16_t count) noexcept {
    const bool carries = count != mold::kHeartbeat && count != mold::kEndOfSession;
    const std::uint64_t advance = carries ? count : 0;
    if (!m_started) {
        m_started = true;
        m_expected = seq + advance;
        return Result::InOrder;
    }
    if (seq == m_expected) {
        m_expected += advance;
        return Result::InOrder;
    }
    if (seq > m_expected) {
        ++m_gaps;
        m_missed += seq - m_expected;
        m_expected = seq + advance;
        return Result::Gap;
    }
    if (carries && seq + advance > m_expected) {
        m_expected = seq + advance;
        return Result::InOrder;
    }
    ++m_stale;
    return Result::Stale;
}

void SequenceTracker::reset() noexcept {
    m_expected = 0;
    m_gaps = 0;
    m_missed = 0;
    m_stale = 0;
    m_started = false;
}

bool SequenceTracker::started() const noexcept {
    return m_started;
}

std::uint64_t SequenceTracker::expected() const noexcept {
    return m_expected;
}

std::uint64_t SequenceTracker::gaps() const noexcept {
    return m_gaps;
}

std::uint64_t SequenceTracker::missed() const noexcept {
    return m_missed;
}

std::uint64_t SequenceTracker::stale() const noexcept {
    return m_stale;
}

}
