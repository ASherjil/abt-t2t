//
// Feed-driven L2 order book (DUT side) — definitions.
//

#include "t2t/dut/BookBuilder.hpp"

#include <algorithm>
#include <cmath>

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
      m_bandFraction(cfg.bandFraction),
      m_baseTick(cfg.tickWire),
      m_subDollarTick(cfg.subDollarTickWire),
      m_anchored(false),
      m_bidSize(cfg.memory == nullptr ? std::pmr::get_default_resource() : cfg.memory),
      m_askSize(cfg.memory == nullptr ? std::pmr::get_default_resource() : cfg.memory),
      m_orders(cfg.maxOrders, cfg.memory) {
    m_orders.countGrowsIn(cfg.rehashes);
}

void BookBuilder::anchor(Price price) {
    if (m_anchored) {
        ++m_reanchors;
    }
    m_tickWire       = (m_subDollarTick > 0 && price < 10000) ? m_subDollarTick : m_baseTick;
    m_tickDiv        = util::DivBy(static_cast<std::uint32_t>(m_tickWire));
    std::size_t band = m_bandTicks;
    if (m_bandFraction > 0.0) {
        const double byPrice = std::floor(static_cast<double>(price) * m_bandFraction /
                                          static_cast<double>(m_tickWire));
        if (byPrice > static_cast<double>(band)) {
            band = static_cast<std::size_t>(byPrice);
        }
    }
    const Price span    = static_cast<Price>(band) * m_tickWire;
    const Price aligned = price - price % m_tickWire;
    m_minPrice          = aligned - span;
    if (m_minPrice < 0) {
        m_minPrice = 0;
    }
    m_maxPrice = m_minPrice + 2 * span;
    m_bidSize.assign(2 * band + 1, 0);
    m_askSize.assign(2 * band + 1, 0);
    m_bestBid  = kNoPrice;
    m_bestAsk  = kNoPrice;
    m_anchored = true;
    rebuildLevels();
}

void BookBuilder::rebuildLevels() {
    m_orders.forEach([this](OrderId, const Resting& r) {
        if (!r.own) {
            addShares(r.side, r.price, r.shares);
        }
    });
}

void BookBuilder::onTradePrice(Price price) {
    if (!inBand(price)) [[unlikely]] {
        anchor(price);
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
    m_own     = 0;
    m_bestBid = kNoPrice;
    m_bestAsk = kNoPrice;
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
    if (!m_anchored || offGrid(price)) [[unlikely]] {
        anchor(price);
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
    if (offGrid(price)) [[unlikely]] {
        anchor(price);
    }
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
    if (trade && !inBand(o->price)) [[unlikely]] {
        anchor(o->price);
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
        return;
    }
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
    if (!inBand(price)) {
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

bool BookBuilder::inBand(Price price) const noexcept {
    return price >= m_minPrice && price <= m_maxPrice;
}

bool BookBuilder::offGrid(Price price) const noexcept {
    return m_bandTicks != 0 && m_subDollarTick > 0 && m_tickWire != m_subDollarTick &&
           price % m_tickWire != 0;
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
