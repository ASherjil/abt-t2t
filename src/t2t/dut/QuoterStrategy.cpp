//
// Realistic market-making quoter — definitions.
//

#include "t2t/dut/QuoterStrategy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace abt::dut {

QuoterStrategy::QuoterStrategy(const QuoterConfig& cfg) noexcept
    : m_cfg(cfg) {
}

bool QuoterStrategy::onBook(const BookBuilder& book, const Account& acct, QuoteTargets& out) noexcept {
    const Price bb = book.bestBid();
    const Price ba = book.bestAsk();
    if (bb == kNoPrice || ba == kNoPrice) {
        forget();
        out = QuoteTargets{};
        return true;
    }
    const std::uint64_t bidSz = book.sizeAt(Side::Buy, bb);
    const std::uint64_t askSz = book.sizeAt(Side::Sell, ba);
    const std::uint64_t total = bidSz + askSz;
    const double        num   = total == 0 ? static_cast<double>(bb) + static_cast<double>(ba)
                                           : static_cast<double>(static_cast<std::uint64_t>(bb) * askSz +
                                                                 static_cast<std::uint64_t>(ba) * bidSz);
    const double        den   = total == 0 ? 2.0 : static_cast<double>(total);
    if (num > m_fairLo * den && num < m_fairHi * den) {
        return false;
    }
    const double fair = num / den;
    const double tick = static_cast<double>(m_cfg.tickWire);
    const double half = static_cast<double>(m_cfg.halfSpreadTicks) * tick;
    const double skew = -static_cast<double>(acct.position) * m_cfg.skewTicksPerUnit * tick;

    const Price origin   = book.bandLow();
    Price       bidPrice = roundDownToTick(fair - half + skew, origin);
    Price       askPrice = roundUpToTick(fair + half + skew, origin);
    const bool  plain    = bidPrice > origin && askPrice > origin && bidPrice < askPrice &&
                       bidPrice >= book.bandLow() && askPrice <= book.bandHigh();
    if (plain) {
        m_fairLo = std::max(static_cast<double>(bidPrice) + half - skew,
                            static_cast<double>(askPrice - m_cfg.tickWire) - half - skew);
        m_fairHi = std::min(static_cast<double>(bidPrice + m_cfg.tickWire) + half - skew,
                            static_cast<double>(askPrice) - half - skew);
    } else {
        forget();
    }
    if (bidPrice >= askPrice) {
        bidPrice = askPrice - m_cfg.tickWire;
    }
    bidPrice = clampToBand(bidPrice, book);
    askPrice = clampToBand(askPrice, book);

    out.quoteBid = true;
    out.bidPrice = bidPrice;
    out.bidQty   = m_cfg.quoteQty;
    out.quoteAsk = true;
    out.askPrice = askPrice;
    out.askQty   = m_cfg.quoteQty;
    return true;
}

void QuoterStrategy::forget() noexcept {
    m_fairLo = 0.0;
    m_fairHi = -1.0;
}

Price QuoterStrategy::roundDownToTick(double price, Price origin) const noexcept {
    if (price <= static_cast<double>(origin)) {
        return origin;
    }
    const double offset = price - static_cast<double>(origin);
    const double ticks  = std::floor(offset / static_cast<double>(m_cfg.tickWire));
    return origin + static_cast<Price>(ticks) * m_cfg.tickWire;
}

Price QuoterStrategy::roundUpToTick(double price, Price origin) const noexcept {
    if (price <= static_cast<double>(origin)) {
        return origin;
    }
    const double offset = price - static_cast<double>(origin);
    const double ticks  = std::ceil(offset / static_cast<double>(m_cfg.tickWire));
    return origin + static_cast<Price>(ticks) * m_cfg.tickWire;
}

Price QuoterStrategy::clampToBand(Price price, const BookBuilder& book) noexcept {
    if (price < book.bandLow()) {
        return book.bandLow();
    }
    if (price > book.bandHigh()) {
        return book.bandHigh();
    }
    return price;
}

}   // namespace abt::dut
