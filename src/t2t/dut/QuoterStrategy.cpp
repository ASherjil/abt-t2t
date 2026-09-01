//
// Realistic market-making quoter — definitions.
//

#include "t2t/dut/QuoterStrategy.hpp"

#include <cmath>
#include <cstdint>

namespace abt::dut {

QuoterStrategy::QuoterStrategy(const QuoterConfig& cfg) noexcept
    : m_cfg(cfg) {
}

QuoteTargets QuoterStrategy::onBook(const BookBuilder& book, const Account& acct) noexcept {
    QuoteTargets q{};
    const Price  bb = book.bestBid();
    const Price  ba = book.bestAsk();
    if (bb == kNoPrice || ba == kNoPrice) {
        return q;   // no two-sided market to anchor to -> pull quotes
    }

    const Quantity bidSz = book.sizeAt(Side::Buy, bb);
    const Quantity askSz = book.sizeAt(Side::Sell, ba);

    // Size-weighted micro-price: bid weighted by ask size and ask weighted by bid size, so the fair
    // leans toward the side with the larger resting size (the side under more pressure).
    const std::uint64_t total = static_cast<std::uint64_t>(bidSz) + static_cast<std::uint64_t>(askSz);
    double              fair  = 0.0;
    if (total == 0) {
        fair = (static_cast<double>(bb) + static_cast<double>(ba)) * 0.5;
    } else {
        fair = (static_cast<double>(bb) * static_cast<double>(askSz) +
                static_cast<double>(ba) * static_cast<double>(bidSz)) /
               static_cast<double>(total);
    }

    const double tick = static_cast<double>(m_cfg.tickWire);
    const double half = static_cast<double>(m_cfg.halfSpreadTicks) * tick;
    // Long inventory -> negative skew -> quotes shift down (lean net seller); short -> up.
    const double skew = -static_cast<double>(acct.position) * m_cfg.skewTicksPerUnit * tick;

    Price bidPrice = roundDownToTick(fair - half + skew);
    Price askPrice = roundUpToTick(fair + half + skew);
    if (bidPrice >= askPrice) {
        bidPrice = askPrice - m_cfg.tickWire;
    }
    bidPrice = clampToBand(bidPrice);
    askPrice = clampToBand(askPrice);

    q.quoteBid = true;
    q.bidPrice = bidPrice;
    q.bidQty   = m_cfg.quoteQty;
    q.quoteAsk = true;
    q.askPrice = askPrice;
    q.askQty   = m_cfg.quoteQty;
    return q;
}

Price QuoterStrategy::roundDownToTick(double price) const noexcept {
    if (price <= static_cast<double>(m_cfg.minPrice)) {
        return m_cfg.minPrice;
    }
    const double offset = price - static_cast<double>(m_cfg.minPrice);
    const double ticks  = std::floor(offset / static_cast<double>(m_cfg.tickWire));
    return m_cfg.minPrice + static_cast<Price>(ticks) * m_cfg.tickWire;
}

Price QuoterStrategy::roundUpToTick(double price) const noexcept {
    if (price <= static_cast<double>(m_cfg.minPrice)) {
        return m_cfg.minPrice;
    }
    const double offset = price - static_cast<double>(m_cfg.minPrice);
    const double ticks  = std::ceil(offset / static_cast<double>(m_cfg.tickWire));
    return m_cfg.minPrice + static_cast<Price>(ticks) * m_cfg.tickWire;
}

Price QuoterStrategy::clampToBand(Price price) const noexcept {
    if (price < m_cfg.minPrice) {
        return m_cfg.minPrice;
    }
    if (price > m_cfg.maxPrice) {
        return m_cfg.maxPrice;
    }
    return price;
}

}   // namespace abt::dut
