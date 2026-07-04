#pragma once
//
// Price-time-priority limit order book / matching engine (flat level arrays, O(1) hot path).
//

#include <cstddef>
#include <vector>

#include "abt/lob/Types.hpp"

namespace abt {

class OrderBook {
public:
    OrderBook(Price minPrice, Price maxPrice, std::size_t poolReserve = 1u << 16)
        : m_minPrice(minPrice),
          m_maxPrice(maxPrice),
          m_bidLevels(levelCount()),
          m_askLevels(levelCount()) {
        m_pool.reserve(poolReserve);
    }

    template <class Sink>
    Handle add(OrderId id, Side side, Price price, Quantity qty, Sink&& sink) {
        if (qty == 0 || price < m_minPrice || price > m_maxPrice) [[unlikely]] {
            return kNilHandle;
        }
        if (side == Side::Buy) {
            while (qty > 0 && m_bestAsk != kNoPrice && price >= m_bestAsk) {
                Level& lvl = m_askLevels[index(m_bestAsk)];
                qty = matchLevel(lvl, id, Side::Buy, qty, sink);
                if (lvl.totalQty == 0) rescanBestAsk();
            }
            if (qty > 0) return rest(id, Side::Buy, price, qty);
        } else {
            while (qty > 0 && m_bestBid != kNoPrice && price <= m_bestBid) {
                Level& lvl = m_bidLevels[index(m_bestBid)];
                qty = matchLevel(lvl, id, Side::Sell, qty, sink);
                if (lvl.totalQty == 0) rescanBestBid();
            }
            if (qty > 0) return rest(id, Side::Sell, price, qty);
        }
        return kNilHandle;
    }

    Handle add(OrderId id, Side side, Price price, Quantity qty) {
        NullSink sink;
        return add(id, side, price, qty, sink);
    }

    Quantity cancel(Handle h) {
        if (h >= m_pool.size()) [[unlikely]] return 0;
        Order& o = m_pool[h];
        if (o.qty == 0) return 0;
        const Quantity removed = o.qty;
        const Price p = o.price;
        const Side  s = o.side;
        Level& lvl = levelFor(s, p);
        unlink(lvl, h);
        lvl.totalQty -= removed;
        --lvl.count;
        o.qty = 0;
        free(h);
        if (lvl.totalQty == 0) {
            if (s == Side::Buy && p == m_bestBid) rescanBestBid();
            else if (s == Side::Sell && p == m_bestAsk) rescanBestAsk();
        }
        return removed;
    }

    Quantity reduce(Handle h, Quantity newQty) {
        if (h >= m_pool.size()) [[unlikely]] return 0;
        Order& o = m_pool[h];
        if (o.qty == 0 || newQty >= o.qty) return 0;
        if (newQty == 0) return cancel(h);
        const Quantity delta = o.qty - newQty;
        o.qty = newQty;
        levelFor(o.side, o.price).totalQty -= delta;
        return delta;
    }

    [[nodiscard]] Price bestBid() const noexcept { return m_bestBid; }
    [[nodiscard]] Price bestAsk() const noexcept { return m_bestAsk; }

    [[nodiscard]] Quantity volumeAt(Side s, Price p) const noexcept {
        if (p < m_minPrice || p > m_maxPrice) return 0;
        return (s == Side::Buy ? m_bidLevels : m_askLevels)[index(p)].totalQty;
    }
    [[nodiscard]] const Order& order(Handle h) const noexcept { return m_pool[h]; }
    [[nodiscard]] bool empty() const noexcept {
        return m_bestBid == kNoPrice && m_bestAsk == kNoPrice;
    }

private:
    struct Level {
        Quantity      totalQty = 0;
        Handle        head     = kNilHandle;
        Handle        tail     = kNilHandle;
        std::uint32_t count    = 0;
    };

    [[nodiscard]] std::size_t levelCount() const noexcept {
        return static_cast<std::size_t>(m_maxPrice - m_minPrice) + 1;
    }
    [[nodiscard]] std::size_t index(Price p) const noexcept {
        return static_cast<std::size_t>(p - m_minPrice);
    }
    [[nodiscard]] Level& levelFor(Side s, Price p) noexcept {
        return (s == Side::Buy ? m_bidLevels : m_askLevels)[index(p)];
    }

    Handle alloc() {
        if (m_freeHead != kNilHandle) {
            const Handle h = m_freeHead;
            m_freeHead = m_pool[h].next;
            return h;
        }
        m_pool.push_back(Order{});
        return static_cast<Handle>(m_pool.size() - 1);
    }
    void free(Handle h) noexcept {
        m_pool[h].next = m_freeHead;
        m_freeHead = h;
    }

    void unlink(Level& lvl, Handle h) noexcept {
        Order& o = m_pool[h];
        if (o.prev != kNilHandle) m_pool[o.prev].next = o.next;
        else                      lvl.head = o.next;
        if (o.next != kNilHandle) m_pool[o.next].prev = o.prev;
        else                      lvl.tail = o.prev;
    }

    template <class Sink>
    Quantity matchLevel(Level& lvl, OrderId aggId, Side aggSide, Quantity qty, Sink& sink) {
        while (qty > 0 && lvl.head != kNilHandle) {
            const Handle rh = lvl.head;
            Order& r = m_pool[rh];
            const Quantity fill = qty < r.qty ? qty : r.qty;
            const bool restingFilled = (fill == r.qty);
            sink.onTrade(Trade{r.id, aggId, r.price, fill, aggSide, restingFilled});
            qty -= fill;
            r.qty -= fill;
            lvl.totalQty -= fill;
            if (restingFilled) {
                lvl.head = r.next;
                if (lvl.head != kNilHandle) m_pool[lvl.head].prev = kNilHandle;
                else                        lvl.tail = kNilHandle;
                --lvl.count;
                free(rh);
            }
        }
        return qty;
    }

    Handle rest(OrderId id, Side side, Price price, Quantity qty) {
        const Handle h = alloc();
        Order& o = m_pool[h];
        o.id = id; o.price = price; o.qty = qty; o.side = side;
        o.prev = kNilHandle; o.next = kNilHandle;

        Level& lvl = levelFor(side, price);
        if (lvl.tail == kNilHandle) {
            lvl.head = lvl.tail = h;
        } else {
            m_pool[lvl.tail].next = h;
            o.prev = lvl.tail;
            lvl.tail = h;
        }
        lvl.totalQty += qty;
        ++lvl.count;

        if (side == Side::Buy) {
            if (m_bestBid == kNoPrice || price > m_bestBid) m_bestBid = price;
        } else {
            if (m_bestAsk == kNoPrice || price < m_bestAsk) m_bestAsk = price;
        }
        return h;
    }

    void rescanBestBid() noexcept {
        for (Price p = m_bestBid; p >= m_minPrice; --p) {
            if (m_bidLevels[index(p)].totalQty > 0) { m_bestBid = p; return; }
        }
        m_bestBid = kNoPrice;
    }
    void rescanBestAsk() noexcept {
        for (Price p = m_bestAsk; p <= m_maxPrice; ++p) {
            if (m_askLevels[index(p)].totalQty > 0) { m_bestAsk = p; return; }
        }
        m_bestAsk = kNoPrice;
    }

    Price m_minPrice;
    Price m_maxPrice;
    Price m_bestBid = kNoPrice;
    Price m_bestAsk = kNoPrice;

    std::vector<Level> m_bidLevels;
    std::vector<Level> m_askLevels;
    std::vector<Order> m_pool;
    Handle m_freeHead = kNilHandle;
};

}
