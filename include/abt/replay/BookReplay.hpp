#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "abt/dut/BookBuilder.hpp"
#include "abt/lob/Types.hpp"
#include "abt/util/Histogram.hpp"

namespace abt::replay {

struct ReplayStats {
    std::uint64_t messages      = 0;
    std::uint64_t adds          = 0;
    std::uint64_t executes      = 0;
    std::uint64_t cancels       = 0;
    std::uint64_t deletes       = 0;
    std::uint64_t replaces      = 0;
    std::uint64_t trades        = 0;
    std::uint64_t unknownRef    = 0;
    std::uint64_t overReduce    = 0;
    std::uint64_t crossed       = 0;
    std::uint64_t outOfBand     = 0;
    std::uint64_t maxLive       = 0;
    std::uint64_t firstTs       = 0;
    std::uint64_t lastTs        = 0;
    std::uint64_t marketOpenTs  = 0;
    std::uint64_t marketCloseTs = 0;
    std::uint64_t peakPerMs     = 0;
    std::uint64_t peakPerSec    = 0;
    std::uint64_t peakMsBucket  = 0;
    std::uint64_t peakSecBucket = 0;
};

class BookReplay {
public:
    BookReplay(std::uint16_t stockLocate, Price minPrice, Price maxPrice, Price tickWire);

    void onMessage(std::span<const std::byte> msg);
    void finish() noexcept;

    [[nodiscard]] const ReplayStats&      stats() const noexcept;
    [[nodiscard]] const dut::BookBuilder& book() const noexcept;
    [[nodiscard]] const util::Histogram&  interArrivalNs() const noexcept;
    [[nodiscard]] bool                    inContinuousSession() const noexcept;

private:
    void checkReference(OrderId ref, Quantity reduceBy) noexcept;
    void rate(std::uint64_t ts) noexcept;
    void checkCrossed() noexcept;

    std::uint16_t    m_locate;
    Price            m_minPrice;
    Price            m_maxPrice;
    dut::BookBuilder m_book;
    ReplayStats      m_stats{};
    util::Histogram  m_gap;
    bool             m_marketHours = false;
    bool             m_trading     = true;
    std::uint64_t    m_prevTs      = 0;
    std::uint64_t    m_msBucket    = 0;
    std::uint64_t    m_msCount     = 0;
    std::uint64_t    m_secBucket   = 0;
    std::uint64_t    m_secCount    = 0;
};

}   // namespace abt::replay
