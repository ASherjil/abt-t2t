//
// Feed-driven L2 order book (DUT side) — definitions.
//

#include "t2t/dut/BookBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace abt::dut {

BookBuilder::BookBuilder(Price minPrice, Price maxPrice, Price tickWire, std::size_t maxOrders,
                         OrderId ownRefMin)
    : m_minPrice(minPrice),
      m_maxPrice(maxPrice),
      m_tickWire(tickWire),
      m_ownRefMin(ownRefMin),
      m_tickDiv(static_cast<std::uint32_t>(tickWire)),
      m_bidSize(static_cast<std::size_t>((maxPrice - minPrice) / tickWire) + 1, 0),
      m_askSize(static_cast<std::size_t>((maxPrice - minPrice) / tickWire) + 1, 0),
      m_orders(maxOrders) {
}

BookBuilder::BookBuilder(const BookConfig& cfg)
    : m_minPrice(0),
      m_maxPrice(-1),
      m_tickWire(cfg.tickWire),
      m_ownRefMin(cfg.ownRefMin),
      m_tickDiv(static_cast<std::uint32_t>(cfg.tickWire)),
      m_bandTicks(cfg.bandTicks),
      m_maxBandTicks(cfg.maxBandTicks),
      m_bandFraction(cfg.bandFraction),
      m_baseTick(cfg.tickWire),
      m_subDollarTick(cfg.subDollarTickWire),
      m_anchored(false),
      m_bidSize(cfg.memory == nullptr ? std::pmr::get_default_resource() : cfg.memory),
      m_askSize(cfg.memory == nullptr ? std::pmr::get_default_resource() : cfg.memory),
      m_orders(cfg.maxOrders, cfg.memory) {
    m_orders.countGrowsIn(cfg.rehashes);
    m_reanchorsOut = cfg.reanchors;
    if (cfg.anchorPrice != kNoPrice) {
        anchor(cfg.anchorPrice, true);
    }
}

void BookBuilder::anchor(Price price, bool trusted) {
    const Price tick = tickFor(price);
    std::size_t band = m_bandTicks;
    if (trusted && m_bandFraction > 0.0) {
        const double byPrice = std::floor(static_cast<double>(price) * m_bandFraction /
                                          static_cast<double>(tick));
        if (byPrice > static_cast<double>(band)) {
            band = static_cast<std::size_t>(byPrice);
        }
    }
    if (m_maxBandTicks != 0 && band > m_maxBandTicks) {
        band = m_maxBandTicks;
    }
    const Price span    = static_cast<Price>(band) * tick;
    const Price aligned = price - price % tick;
    Price       newMin  = aligned - span;
    if (newMin < 0) {
        newMin = 0;
    }
    const Price newMax = newMin + 2 * span;

    const bool shiftable = m_anchored && tick == m_tickWire &&
                           (m_parkedShares == 0 || newMax < m_parkedLo || newMin > m_parkedHi);
    if (m_anchored) {
        ++m_reanchors;
        if (m_reanchorsOut != nullptr) {
            ++*m_reanchorsOut;
        }
    }
    if (shiftable) {
        shiftLevels(m_bidSize, newMin, newMax);
        shiftLevels(m_askSize, newMin, newMax);
        m_minPrice = newMin;
        m_maxPrice = newMax;
        recomputeBest();
        return;
    }
    m_tickWire = tick;
    m_tickDiv  = util::DivBy(static_cast<std::uint32_t>(tick));
    m_minPrice = newMin;
    m_maxPrice = newMax;
    m_bidSize.assign(2 * band + 1, 0);
    m_askSize.assign(2 * band + 1, 0);
    m_bestBid  = kNoPrice;
    m_bestAsk  = kNoPrice;
    m_anchored = true;
    rebuildLevels();
}

void BookBuilder::shiftLevels(std::pmr::vector<Quantity>& levels, Price newMin, Price newMax) noexcept {
    const std::size_t oldN = levels.size();
    const std::size_t newN = static_cast<std::size_t>((newMax - newMin) / m_tickWire) + 1;
    const Price       lo   = std::max(m_minPrice, newMin);
    const Price       hi   = std::min(m_maxPrice, newMax);
    std::size_t       oi0  = 0;
    std::size_t       nj0  = 0;
    std::size_t       len  = 0;
    if (lo <= hi) {
        oi0 = index(lo);
        nj0 = static_cast<std::size_t>((lo - newMin) / m_tickWire);
        len = static_cast<std::size_t>((hi - lo) / m_tickWire) + 1;
    }
    for (std::size_t i = 0; i < oldN; ++i) {
        if ((i < oi0 || i >= oi0 + len) && levels[i] != 0) {
            park(m_minPrice + static_cast<Price>(i) * m_tickWire, levels[i]);
        }
    }
    if (newN > oldN) {
        levels.resize(newN, 0);
    }
    if (len > 0 && nj0 != oi0) {
        std::memmove(levels.data() + nj0, levels.data() + oi0, len * sizeof(Quantity));
    }
    std::fill(levels.begin(), levels.begin() + static_cast<std::ptrdiff_t>(nj0), 0u);
    std::fill(levels.begin() + static_cast<std::ptrdiff_t>(nj0 + len), levels.end(), 0u);
    if (newN < levels.size()) {
        levels.resize(newN);
    }
}

void BookBuilder::recomputeBest() noexcept {
    if (m_bestBid == kNoPrice || !inBand(m_bestBid) || m_bidSize[index(m_bestBid)] == 0) {
        const std::size_t j = util::scanDownNonZero(m_bidSize.data(), m_bidSize.size() - 1);
        m_bestBid = j == util::kNoIndex ? kNoPrice : m_minPrice + static_cast<Price>(j) * m_tickWire;
    }
    if (m_bestAsk == kNoPrice || !inBand(m_bestAsk) || m_askSize[index(m_bestAsk)] == 0) {
        const std::size_t j = util::scanUpNonZero(m_askSize.data(), 0, m_askSize.size() - 1);
        m_bestAsk = j == util::kNoIndex ? kNoPrice : m_minPrice + static_cast<Price>(j) * m_tickWire;
    }
}

void BookBuilder::rebuildLevels() {
    m_parkedShares = 0;
    m_parkedLo     = kNoPrice;
    m_parkedHi     = kNoPrice;
    m_orders.forEach([this](OrderId, const Resting& r) {
        if (r.own) {
            return;
        }
        if (inBand(r.price)) {
            addLevel(r.side, r.price, r.shares);
        } else {
            park(r.price, r.shares);
        }
    });
}

void BookBuilder::park(Price price, Quantity shares) noexcept {
    m_parkedShares += shares;
    if (m_parkedLo == kNoPrice || price < m_parkedLo) {
        m_parkedLo = price;
    }
    if (m_parkedHi == kNoPrice || price > m_parkedHi) {
        m_parkedHi = price;
    }
}

void BookBuilder::unpark(Quantity shares) noexcept {
    m_parkedShares -= std::min<std::uint64_t>(shares, m_parkedShares);
    if (m_parkedShares == 0) {
        m_parkedLo = kNoPrice;
        m_parkedHi = kNoPrice;
    }
}

std::uint64_t BookBuilder::parkedShares() const noexcept {
    return m_parkedShares;
}

void BookBuilder::onTradePrice(Price price) {
    if (needsAnchor(price)) [[unlikely]] {
        anchor(price, true);
    }
}

std::uint32_t BookBuilder::reanchors() const noexcept {
    return m_reanchors;
}

Price BookBuilder::tickWire() const noexcept {
    return m_tickWire;
}

std::size_t BookBuilder::footprintBytes() const noexcept {
    return (m_bidSize.capacity() + m_askSize.capacity()) * sizeof(Quantity) +
           m_orders.capacity() * (sizeof(OrderId) + sizeof(Resting)) + sizeof(BookBuilder);
}

bool BookBuilder::anchored() const noexcept {
    return m_anchored;
}

Price BookBuilder::bandLow() const noexcept {
    return m_minPrice;
}

Price BookBuilder::bandHigh() const noexcept {
    return m_maxPrice;
}

std::uint64_t BookBuilder::outOfBandAdds() const noexcept {
    return m_oob;
}

void BookBuilder::apply(std::span<const std::byte> itchMessage) {
    if (itchMessage.empty()) {
        return;
    }
    // The ITCH overlay structs are alignof-1 and trivially copyable, so reading fields directly
    // through the wire pointer is well-defined (C++20 implicit object creation) and avoids copying
    // the whole message onto the stack just to read a handful of fields.
    const std::byte* data = itchMessage.data();
    const char       type = static_cast<char>(itchMessage[0]);
    switch (type) {
        case 'A':
        case 'F': {
            if (itchMessage.size() >= sizeof(itch::AddOrder)) {
                onAddOrder(*reinterpret_cast<const itch::AddOrder*>(data));
            }
            break;
        }
        case 'E': {
            if (itchMessage.size() >= sizeof(itch::OrderExecuted)) {
                const auto* e = reinterpret_cast<const itch::OrderExecuted*>(data);
                reduceOrder(e->orderRef.value(), e->executedShares.value(), true);
            }
            break;
        }
        case 'C': {
            if (itchMessage.size() >= sizeof(itch::OrderExecutedWithPrice)) {
                const auto* c = reinterpret_cast<const itch::OrderExecutedWithPrice*>(data);
                reduceOrder(c->orderRef.value(), c->executedShares.value(), true);
            }
            break;
        }
        case 'X': {
            if (itchMessage.size() >= sizeof(itch::OrderCancel)) {
                const auto* x = reinterpret_cast<const itch::OrderCancel*>(data);
                reduceOrder(x->orderRef.value(), x->cancelledShares.value(), false);
            }
            break;
        }
        case 'P': {
            if (itchMessage.size() >= sizeof(itch::TradeNonCross)) {
                const auto* p = reinterpret_cast<const itch::TradeNonCross*>(data);
                onTradePrice(static_cast<Price>(p->price.value()));
            }
            break;
        }
        case 'Q': {
            if (itchMessage.size() >= sizeof(itch::CrossTrade)) {
                const auto* q = reinterpret_cast<const itch::CrossTrade*>(data);
                if (q->shares.value() > 0 && q->crossPrice.value() > 0) {
                    onTradePrice(static_cast<Price>(q->crossPrice.value()));
                }
            }
            break;
        }
        case 'D': {
            if (itchMessage.size() >= sizeof(itch::OrderDelete)) {
                const auto* d = reinterpret_cast<const itch::OrderDelete*>(data);
                removeOrder(d->orderRef.value());
            }
            break;
        }
        case 'U': {
            if (itchMessage.size() >= sizeof(itch::OrderReplace)) {
                onOrderReplace(*reinterpret_cast<const itch::OrderReplace*>(data));
            }
            break;
        }
        default: {
            break;
        }
    }
}

void BookBuilder::clear() noexcept {
    std::fill(m_bidSize.begin(), m_bidSize.end(), 0u);
    std::fill(m_askSize.begin(), m_askSize.end(), 0u);
    m_orders.clear();
    m_own          = 0;
    m_bestBid      = kNoPrice;
    m_bestAsk      = kNoPrice;
    m_parkedShares = 0;
    m_parkedLo     = kNoPrice;
    m_parkedHi     = kNoPrice;
}

Price BookBuilder::bestBid() const noexcept {
    return m_bestBid;
}

Price BookBuilder::bestAsk() const noexcept {
    return m_bestAsk;
}

Quantity BookBuilder::sizeAt(Side side, Price price) const noexcept {
    if (!inBand(price)) {
        return 0;
    }
    if (side == Side::Buy) {
        return m_bidSize[index(price)];
    }
    return m_askSize[index(price)];
}

Quantity BookBuilder::restingShares(OrderId ref) const noexcept {
    const Resting* o = m_orders.find(ref);
    return o == nullptr ? 0 : o->shares;
}

Price BookBuilder::restingPrice(OrderId ref) const noexcept {
    const Resting* o = m_orders.find(ref);
    return o == nullptr ? kNoPrice : o->price;
}

std::size_t BookBuilder::liveOrders() const noexcept {
    return m_orders.size();
}

std::size_t BookBuilder::orderCapacity() const noexcept {
    return m_orders.capacity();
}

std::size_t BookBuilder::ownOrders() const noexcept {
    return m_own;
}

void BookBuilder::onAddOrder(const itch::AddOrder& msg) {
    const Quantity shares = msg.shares.value();
    if (shares == 0) {
        return;
    }
    const OrderId ref   = msg.orderRef.value();
    const Side    side  = (msg.side == itch::Side::Buy) ? Side::Buy : Side::Sell;
    const Price   price = static_cast<Price>(msg.price.value());
    const bool    own   = m_ownRefMin != 0 && ref >= m_ownRefMin;
    if (!m_anchored) [[unlikely]] {
        anchor(price, false);
    }
    m_orders.insertOrAssign(ref, Resting{.price = price, .shares = shares, .side = side, .own = own});
    if (own) [[unlikely]] {
        ++m_own;
        return;
    }
    addShares(side, price, shares);
}

void BookBuilder::onOrderReplace(const itch::OrderReplace& msg) {
    Resting orig{};
    if (!m_orders.erase(msg.origOrderRef.value(), orig)) {
        return;
    }
    if (!orig.own) {
        removeShares(orig.side, orig.price, orig.shares);
    } else {
        --m_own;
    }

    const Quantity shares = msg.shares.value();
    if (shares == 0) {
        return;
    }
    const Price price = static_cast<Price>(msg.price.value());
    m_orders.insertOrAssign(msg.newOrderRef.value(),
                            Resting{.price = price, .shares = shares, .side = orig.side, .own = orig.own});
    if (orig.own) [[unlikely]] {
        ++m_own;
        return;
    }
    addShares(orig.side, price, shares);
}

void BookBuilder::reduceOrder(OrderId ref, Quantity by, bool trade) {
    Resting* o = m_orders.find(ref);
    if (o == nullptr) {
        return;
    }
    if (trade && needsAnchor(o->price)) [[unlikely]] {
        anchor(o->price, true);
    }
    const Quantity gone = (by < o->shares) ? by : o->shares;
    if (!o->own) {
        removeShares(o->side, o->price, gone);
    }
    o->shares -= gone;
    if (o->shares == 0) {
        if (o->own) {
            --m_own;
        }
        m_orders.erase(ref);
    }
}

void BookBuilder::removeOrder(OrderId ref) {
    Resting o{};
    if (!m_orders.erase(ref, o)) {
        return;
    }
    if (o.own) {
        --m_own;
        return;
    }
    removeShares(o.side, o.price, o.shares);
}

void BookBuilder::addShares(Side side, Price price, Quantity shares) noexcept {
    if (!inBand(price)) [[unlikely]] {
        ++m_oob;
        park(price, shares);
        return;
    }
    addLevel(side, price, shares);
}

void BookBuilder::addLevel(Side side, Price price, Quantity shares) noexcept {
    const std::size_t i = index(price);
    if (side == Side::Buy) {
        m_bidSize[i] += shares;
        if (m_bestBid == kNoPrice || price > m_bestBid) {
            m_bestBid = price;
        }
    } else {
        m_askSize[i] += shares;
        if (m_bestAsk == kNoPrice || price < m_bestAsk) {
            m_bestAsk = price;
        }
    }
}

void BookBuilder::removeShares(Side side, Price price, Quantity shares) noexcept {
    if (!inBand(price)) [[unlikely]] {
        unpark(shares);
        return;
    }
    const std::size_t i = index(price);
    if (side == Side::Buy) {
        if (shares >= m_bidSize[i]) {
            m_bidSize[i] = 0;
        } else {
            m_bidSize[i] -= shares;
        }
        if (m_bidSize[i] == 0 && price == m_bestBid) {
            rescanBestBid();
        }
    } else {
        if (shares >= m_askSize[i]) {
            m_askSize[i] = 0;
        } else {
            m_askSize[i] -= shares;
        }
        if (m_askSize[i] == 0 && price == m_bestAsk) {
            rescanBestAsk();
        }
    }
}

bool BookBuilder::inRange(Price price) const noexcept {
    return price >= m_minPrice && price <= m_maxPrice;
}

bool BookBuilder::inBand(Price price) const noexcept {
    if (!inRange(price)) {
        return false;
    }
    const auto off = static_cast<std::uint32_t>(price - m_minPrice);
    return off == m_tickDiv(off) * static_cast<std::uint32_t>(m_tickWire);
}

Price BookBuilder::tickFor(Price price) const noexcept {
    return (m_subDollarTick > 0 && price < 10000) ? m_subDollarTick : m_baseTick;
}

bool BookBuilder::needsAnchor(Price price) const noexcept {
    return !inRange(price) || tickFor(price) != m_tickWire;
}

std::size_t BookBuilder::index(Price price) const noexcept {
    return m_tickDiv(static_cast<std::uint32_t>(price - m_minPrice));
}

void BookBuilder::rescanBestBid() noexcept {
    // Fall back to the next populated level below the old best. AVX2 checks 8 levels per step,
    // which matters when the book has gaps and the next level is far down.
    const std::size_t j = util::scanDownNonZero(m_bidSize.data(), index(m_bestBid));
    if (j == util::kNoIndex) {
        m_bestBid = kNoPrice;
    } else {
        m_bestBid = m_minPrice + static_cast<Price>(j) * m_tickWire;
    }
}

void BookBuilder::rescanBestAsk() noexcept {
    const std::size_t last = m_askSize.size() - 1;
    const std::size_t j    = util::scanUpNonZero(m_askSize.data(), index(m_bestAsk), last);
    if (j == util::kNoIndex) {
        m_bestAsk = kNoPrice;
    } else {
        m_bestAsk = m_minPrice + static_cast<Price>(j) * m_tickWire;
    }
}

}   // namespace abt::dut
