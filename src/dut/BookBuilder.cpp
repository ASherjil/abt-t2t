//
// Feed-driven L2 order book (DUT side) — definitions.
//

#include "abt/dut/BookBuilder.hpp"

namespace abt::dut {

BookBuilder::BookBuilder(Price minPrice, Price maxPrice, Price tickWire, std::size_t maxOrders)
    : m_minPrice(minPrice),
      m_maxPrice(maxPrice),
      m_tickWire(tickWire),
      m_tickDiv(static_cast<std::uint32_t>(tickWire)),
      m_bidSize(static_cast<std::size_t>((maxPrice - minPrice) / tickWire) + 1, 0),
      m_askSize(static_cast<std::size_t>((maxPrice - minPrice) / tickWire) + 1, 0),
      m_orders(maxOrders) {
}

void BookBuilder::apply(std::span<const std::byte> itchMessage) {
    if (itchMessage.empty()) {
        return;
    }
    // The ITCH overlay structs are alignof-1 and trivially copyable, so reading fields directly
    // through the wire pointer is well-defined (C++20 implicit object creation) and avoids copying
    // the whole message onto the stack just to read a handful of fields.
    const std::byte* data = itchMessage.data();
    const char type = static_cast<char>(itchMessage[0]);
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
                reduceOrder(e->orderRef.value(), e->executedShares.value());
            }
            break;
        }
        case 'C': {
            if (itchMessage.size() >= sizeof(itch::OrderExecutedWithPrice)) {
                const auto* c = reinterpret_cast<const itch::OrderExecutedWithPrice*>(data);
                reduceOrder(c->orderRef.value(), c->executedShares.value());
            }
            break;
        }
        case 'X': {
            if (itchMessage.size() >= sizeof(itch::OrderCancel)) {
                const auto* x = reinterpret_cast<const itch::OrderCancel*>(data);
                reduceOrder(x->orderRef.value(), x->cancelledShares.value());
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

std::size_t BookBuilder::liveOrders() const noexcept {
    return m_orders.size();
}

void BookBuilder::onAddOrder(const itch::AddOrder& msg) {
    const Quantity shares = msg.shares.value();
    if (shares == 0) {
        return;
    }
    const OrderId ref = msg.orderRef.value();
    const Side side = (msg.side == static_cast<char>(itch::Side::Buy)) ? Side::Buy : Side::Sell;
    const Price price = static_cast<Price>(msg.price.value());
    m_orders.insertOrAssign(ref, Resting{price, shares, side});
    addShares(side, price, shares);
}

void BookBuilder::onOrderReplace(const itch::OrderReplace& msg) {
    Resting orig{};
    if (!m_orders.erase(msg.origOrderRef.value(), orig)) {
        return;
    }
    removeShares(orig.side, orig.price, orig.shares);

    const Quantity shares = msg.shares.value();
    if (shares == 0) {
        return;
    }
    const Price price = static_cast<Price>(msg.price.value());
    m_orders.insertOrAssign(msg.newOrderRef.value(), Resting{price, shares, orig.side});
    addShares(orig.side, price, shares);
}

void BookBuilder::reduceOrder(OrderId ref, Quantity by) {
    Resting* o = m_orders.find(ref);
    if (o == nullptr) {
        return;
    }
    const Quantity gone = (by < o->shares) ? by : o->shares;
    removeShares(o->side, o->price, gone);
    o->shares -= gone;
    if (o->shares == 0) {
        m_orders.erase(ref);
    }
}

void BookBuilder::removeOrder(OrderId ref) {
    Resting o{};
    if (!m_orders.erase(ref, o)) {
        return;
    }
    removeShares(o.side, o.price, o.shares);
}

void BookBuilder::addShares(Side side, Price price, Quantity shares) noexcept {
    if (!inBand(price)) {
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

std::size_t BookBuilder::index(Price price) const noexcept {
    return m_tickDiv(static_cast<std::uint32_t>(price - m_minPrice));
}

void BookBuilder::rescanBestBid() noexcept {
    // Walk the level index down in lockstep with the price so the array index advances by one per
    // step instead of recomputing index() (a division) at every level.
    std::size_t i = index(m_bestBid);
    Price p = m_bestBid;
    for (;;) {
        if (m_bidSize[i] > 0) {
            m_bestBid = p;
            return;
        }
        if (i == 0) {
            break;
        }
        --i;
        p -= m_tickWire;
    }
    m_bestBid = kNoPrice;
}

void BookBuilder::rescanBestAsk() noexcept {
    std::size_t i = index(m_bestAsk);
    const std::size_t last = m_askSize.size() - 1;
    Price p = m_bestAsk;
    for (;;) {
        if (m_askSize[i] > 0) {
            m_bestAsk = p;
            return;
        }
        if (i == last) {
            break;
        }
        ++i;
        p += m_tickWire;
    }
    m_bestAsk = kNoPrice;
}

}
