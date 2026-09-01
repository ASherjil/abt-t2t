#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "t2t/lob/OrderBook.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/util/FlatHashMap.hpp"

namespace abt {

struct MirrorStats {
    std::uint64_t adds         = 0;
    std::uint64_t executes     = 0;
    std::uint64_t cancels      = 0;
    std::uint64_t deletes      = 0;
    std::uint64_t replaces     = 0;
    std::uint64_t unknownRef   = 0;
    std::uint64_t overReduce   = 0;
    std::uint64_t outOfBand    = 0;
    std::uint64_t shadowFills  = 0;
    std::uint64_t shadowShares = 0;
    std::uint64_t crossFills   = 0;
    std::uint64_t impactFills  = 0;
    std::uint64_t selfTrades   = 0;
};

template <class Sink>
class Venue {
public:
    Venue(Sink& sink, std::string_view symbol, std::uint16_t stockLocate, Price minTick, Price maxTick,
          std::uint32_t wirePerTick = 100, OrderId firstOrderRef = 1, std::size_t liveReserve = 1u << 12);

    void sessionEvent(itch::SystemEventCode code, std::uint64_t ts);
    void onEnterOrder(const ouch::EnterOrder& o, std::uint64_t ts);
    void onCancelOrder(const ouch::CancelOrder& x, std::uint64_t ts);
    void onReplaceOrder(const ouch::ReplaceOrder& u, std::uint64_t ts);

    OrderId injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts);
    void    cancelSynthetic(OrderId ref, std::uint64_t ts);

    void mirrorAdd(OrderId ref, Side side, std::uint32_t wirePrice, Quantity qty, std::uint64_t ts);
    void mirrorExecute(OrderId ref, Quantity shares, std::uint64_t ts);
    void mirrorCancel(OrderId ref, Quantity shares, std::uint64_t ts);
    void mirrorDelete(OrderId ref, std::uint64_t ts);
    void mirrorReplace(OrderId origRef, OrderId newRef, Quantity shares, std::uint32_t wirePrice,
                       std::uint64_t ts);
    void resetDay(std::uint64_t ts);
    [[nodiscard]] const MirrorStats& mirrorStats() const noexcept;
    [[nodiscard]] std::size_t        clientOrders() const noexcept;

    [[nodiscard]] const OrderBook& book() const noexcept;
    [[nodiscard]] Price            bestBid() const noexcept;
    [[nodiscard]] Price            bestAsk() const noexcept;
    [[nodiscard]] std::uint64_t    trades() const noexcept;
    [[nodiscard]] std::size_t      liveOrders() const noexcept;

private:
    struct LiveOrder {
        Handle        handle  = kNilHandle;
        Side          side    = Side::Buy;
        Price         tick    = 0;
        bool          client  = false;
        std::uint32_t userRef = 0;
    };

    struct TradeEmitter {
        Venue*        v;
        std::uint64_t ts;
        bool          aggClient;
        std::uint32_t aggUser;

        void onTrade(const Trade& t) {
            v->handleTrade(t, ts, aggClient, aggUser);
        }
    };
    friend struct TradeEmitter;

    [[nodiscard]] bool validPrice(std::uint64_t wire, Price& tick) const noexcept;
    [[nodiscard]] bool parseSide(char c, Side& side) const noexcept;
    void     processOrder(OrderId ref, Side side, Price tick, Quantity qty, std::uint64_t ts, bool client,
                          std::uint32_t user);
    void     processImmediate(OrderId ref, Side side, Price tick, Quantity qty, std::uint64_t ts,
                              std::uint32_t user);
    void     handleTrade(const Trade& t, std::uint64_t ts, bool aggClient, std::uint32_t aggUser);
    void     trackClient(OrderId ref, Handle h, Side side, Price tick, std::uint32_t user);
    void     dropClient(OrderId ref, const LiveOrder& live);
    void     fillClient(OrderId ref, Quantity qty, std::uint64_t ts);
    Quantity crossClients(Side side, Price tick, Quantity qty, std::uint64_t ts);
    void     shadowFill(Side side, Price tick, OrderId realRef, Quantity qty, std::uint64_t ts);
    [[nodiscard]] bool aheadInLevel(Side side, Price tick, OrderId clientRef, OrderId realRef) const noexcept;
    void               removeReal(OrderId ref, const LiveOrder& live);
    [[nodiscard]] bool isMarketable(Side side, Price tick) const noexcept;
    [[nodiscard]] std::uint32_t tickToWire(Price tick) const noexcept;

    template <class Msg>
    void sendMd(const Msg& m);
    template <class Msg>
    void sendOe(const Msg& m);

    static char itchSide(Side s) noexcept;

    void emitItchAdd(OrderId ref, Side side, Price tick, Quantity shares, std::uint64_t ts);
    void emitItchExecuted(OrderId ref, Quantity shares, std::uint64_t match, std::uint64_t ts);
    void emitItchCancel(OrderId ref, Quantity shares, std::uint64_t ts);
    void emitItchDelete(OrderId ref, std::uint64_t ts);
    void emitItchReplace(OrderId origRef, OrderId newRef, Quantity shares, Price tick, std::uint64_t ts);
    void emitAccepted(const ouch::EnterOrder& o, OrderId ref, char orderState, std::uint64_t ts);
    void emitExecuted(std::uint32_t user, Quantity shares, Price tick, char liq, std::uint64_t match,
                      std::uint64_t ts);
    void emitCanceled(std::uint32_t user, Quantity decremented, ouch::CancelReason r, std::uint64_t ts);
    void emitReplaced(const ouch::ReplaceOrder& u, OrderId newRef, Side side, std::uint64_t ts);
    void emitRejected(std::uint32_t user, ouch::RejectReason reason, std::string_view clOrdId,
                      std::uint64_t ts);
    void emitCancelReject(std::uint32_t user, std::uint64_t ts);

    Sink&         m_sink;
    std::string   m_symbol;
    std::uint16_t m_stockLocate;
    std::uint32_t m_wirePerTick;
    Price         m_minTick;
    Price         m_maxTick;
    OrderBook     m_engine;

    OrderId       m_nextOrderRef;
    std::uint64_t m_nextMatch = 1;
    MirrorStats   m_mirror{};

    util::FlatHashMap<OrderId, LiveOrder>     m_live;
    util::FlatHashMap<std::uint32_t, OrderId> m_byUserRef;
    std::vector<OrderId>                      m_clientRefs;
};

template <class Sink>
Venue<Sink>::Venue(Sink& sink, std::string_view symbol, std::uint16_t stockLocate, Price minTick,
                   Price maxTick, std::uint32_t wirePerTick, OrderId firstOrderRef, std::size_t liveReserve)
    : m_sink(sink),
      m_symbol(symbol),
      m_stockLocate(stockLocate),
      m_wirePerTick(wirePerTick),
      m_minTick(minTick),
      m_maxTick(maxTick),
      m_engine(minTick, maxTick, liveReserve),
      m_nextOrderRef(firstOrderRef == 0 ? 1 : firstOrderRef),
      m_live(liveReserve),
      m_byUserRef(1u << 12) {
    m_clientRefs.reserve(16);
}

template <class Sink>
void Venue<Sink>::sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
    itch::SystemEvent s{};
    s.messageType    = static_cast<char>(itch::MessageType::SystemEvent);
    s.stockLocate    = 0;
    s.trackingNumber = 0;
    s.timestamp      = ts;
    s.eventCode      = static_cast<char>(code);
    sendMd(s);
}

template <class Sink>
void Venue<Sink>::onEnterOrder(const ouch::EnterOrder& o, std::uint64_t ts) {
    const std::uint32_t user = o.userRefNum.value();
    Side                side = Side::Buy;
    Price               tick = 0;
    if (!parseSide(o.side, side)) {
        emitRejected(user, ouch::RejectReason::InvalidSide, o.clOrdId.view(), ts);
        return;
    }
    if (o.quantity.value() == 0) {
        emitRejected(user, ouch::RejectReason::InvalidQuantity, o.clOrdId.view(), ts);
        return;
    }
    if (o.symbol.view() != m_symbol) {
        emitRejected(user, ouch::RejectReason::InvalidSymbol, o.clOrdId.view(), ts);
        return;
    }
    if (!validPrice(o.price.value(), tick)) {
        emitRejected(user, ouch::RejectReason::InvalidPrice, o.clOrdId.view(), ts);
        return;
    }

    const OrderId ref       = m_nextOrderRef++;
    const bool    immediate = o.timeInForce == static_cast<char>(ouch::TimeInForce::IOC);
    const char    state     = immediate ? static_cast<char>(ouch::OrderState::Dead)
                                        : static_cast<char>(ouch::OrderState::Live);
    emitAccepted(o, ref, state, ts);
    if (immediate) {
        processImmediate(ref, side, tick, o.quantity.value(), ts, user);
    } else {
        processOrder(ref, side, tick, o.quantity.value(), ts, true, user);
    }
}

template <class Sink>
void Venue<Sink>::onCancelOrder(const ouch::CancelOrder& x, std::uint64_t ts) {
    const std::uint32_t user = x.userRefNum.value();
    const OrderId*      refp = m_byUserRef.find(user);
    if (refp == nullptr) {
        emitCancelReject(user, ts);
        return;
    }
    const OrderId    ref  = *refp;
    const LiveOrder* live = m_live.find(ref);
    if (live == nullptr) {
        emitCancelReject(user, ts);
        return;
    }

    const Handle   h        = live->handle;
    const Quantity cur      = m_engine.order(h).qty;
    const Quantity intended = x.quantity.value();
    if (intended >= cur) {
        emitCancelReject(user, ts);
        return;
    }

    if (intended == 0) {
        const Quantity removed = m_engine.cancel(h);
        emitItchDelete(ref, ts);
        emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
        dropClient(ref, *live);
    } else {
        const Quantity removed = m_engine.reduce(h, intended);
        emitItchCancel(ref, removed, ts);
        emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
    }
}

template <class Sink>
void Venue<Sink>::onReplaceOrder(const ouch::ReplaceOrder& u, std::uint64_t ts) {
    const std::uint32_t origUser = u.origUserRefNum.value();
    const std::uint32_t newUser  = u.userRefNum.value();
    const OrderId*      refp     = m_byUserRef.find(origUser);
    if (refp == nullptr) {
        emitRejected(newUser, ouch::RejectReason::ReplaceNotAllowed, u.clOrdId.view(), ts);
        return;
    }
    const OrderId    origRef = *refp;
    const LiveOrder* live    = m_live.find(origRef);
    if (live == nullptr) {
        emitRejected(newUser, ouch::RejectReason::ReplaceNotAllowed, u.clOrdId.view(), ts);
        return;
    }
    const Quantity qty = u.quantity.value();
    if (qty == 0) {
        emitRejected(newUser, ouch::RejectReason::InvalidQuantity, u.clOrdId.view(), ts);
        return;
    }
    Price tick = 0;
    if (!validPrice(u.price.value(), tick)) {
        emitRejected(newUser, ouch::RejectReason::InvalidPrice, u.clOrdId.view(), ts);
        return;
    }

    const LiveOrder orig = *live;
    const Side      side = orig.side;

    m_engine.cancel(orig.handle);
    dropClient(origRef, orig);

    const OrderId newRef = m_nextOrderRef++;
    emitReplaced(u, newRef, side, ts);

    if (isMarketable(side, tick)) {
        emitItchDelete(origRef, ts);
        processOrder(newRef, side, tick, qty, ts, true, newUser);
    } else {
        const Handle h = m_engine.add(newRef, side, tick, qty);
        emitItchReplace(origRef, newRef, qty, tick, ts);
        if (h != kNilHandle) {
            trackClient(newRef, h, side, tick, newUser);
        }
    }
}

template <class Sink>
OrderId Venue<Sink>::injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts) {
    const OrderId ref = m_nextOrderRef++;
    processOrder(ref, side, tick, qty, ts, false, 0);
    return ref;
}

template <class Sink>
void Venue<Sink>::cancelSynthetic(OrderId ref, std::uint64_t ts) {
    LiveOrder live{};
    if (!m_live.erase(ref, live)) {
        return;
    }
    m_engine.cancel(live.handle);
    emitItchDelete(ref, ts);
}

template <class Sink>
void Venue<Sink>::mirrorAdd(OrderId ref, Side side, std::uint32_t wirePrice, Quantity qty, std::uint64_t ts) {
    ++m_mirror.adds;
    Price tick = 0;
    if (!validPrice(wirePrice, tick)) {
        ++m_mirror.outOfBand;
        return;
    }
    Quantity rem = qty;
    if (!m_clientRefs.empty()) {
        rem = crossClients(side, tick, qty, ts);
    }
    if (rem == 0) {
        return;
    }
    const Handle h = m_engine.place(ref, side, tick, rem);
    if (h != kNilHandle) {
        m_live.insertOrAssign(ref, LiveOrder{h, side, tick, false, 0});
    }
}

template <class Sink>
void Venue<Sink>::mirrorExecute(OrderId ref, Quantity shares, std::uint64_t ts) {
    ++m_mirror.executes;
    const LiveOrder* live = m_live.find(ref);
    if (live == nullptr) {
        ++m_mirror.unknownRef;
        return;
    }
    const LiveOrder l = *live;
    if (!m_clientRefs.empty()) {
        shadowFill(l.side, l.tick, ref, shares, ts);
    }
    const Quantity cur = m_engine.order(l.handle).qty;
    if (shares > cur) {
        ++m_mirror.overReduce;
    }
    if (shares >= cur) {
        removeReal(ref, l);
    } else {
        m_engine.reduce(l.handle, cur - shares);
    }
}

template <class Sink>
void Venue<Sink>::mirrorCancel(OrderId ref, Quantity shares, std::uint64_t) {
    ++m_mirror.cancels;
    const LiveOrder* live = m_live.find(ref);
    if (live == nullptr) {
        ++m_mirror.unknownRef;
        return;
    }
    const LiveOrder l   = *live;
    const Quantity  cur = m_engine.order(l.handle).qty;
    if (shares > cur) {
        ++m_mirror.overReduce;
    }
    if (shares >= cur) {
        removeReal(ref, l);
    } else {
        m_engine.reduce(l.handle, cur - shares);
    }
}

template <class Sink>
void Venue<Sink>::mirrorDelete(OrderId ref, std::uint64_t) {
    ++m_mirror.deletes;
    const LiveOrder* live = m_live.find(ref);
    if (live == nullptr) {
        ++m_mirror.unknownRef;
        return;
    }
    removeReal(ref, *live);
}

template <class Sink>
void Venue<Sink>::mirrorReplace(OrderId origRef, OrderId newRef, Quantity shares, std::uint32_t wirePrice,
                                std::uint64_t ts) {
    ++m_mirror.replaces;
    const LiveOrder* live = m_live.find(origRef);
    if (live == nullptr) {
        ++m_mirror.unknownRef;
        return;
    }
    const Side side = live->side;
    removeReal(origRef, *live);
    --m_mirror.adds;
    mirrorAdd(newRef, side, wirePrice, shares, ts);
}

template <class Sink>
void Venue<Sink>::resetDay(std::uint64_t ts) {
    while (!m_clientRefs.empty()) {
        const OrderId    ref  = m_clientRefs.back();
        const LiveOrder* live = m_live.find(ref);
        if (live == nullptr) {
            m_clientRefs.pop_back();
            continue;
        }
        const Quantity removed = m_engine.cancel(live->handle);
        emitItchDelete(ref, ts);
        emitCanceled(live->userRef, removed, ouch::CancelReason::Closed, ts);
        dropClient(ref, *live);
    }
    m_engine.clear();
    m_live.clear();
    m_byUserRef.clear();
}

template <class Sink>
const MirrorStats& Venue<Sink>::mirrorStats() const noexcept {
    return m_mirror;
}

template <class Sink>
std::size_t Venue<Sink>::clientOrders() const noexcept {
    return m_clientRefs.size();
}

template <class Sink>
const OrderBook& Venue<Sink>::book() const noexcept {
    return m_engine;
}

template <class Sink>
Price Venue<Sink>::bestBid() const noexcept {
    return m_engine.bestBid();
}

template <class Sink>
Price Venue<Sink>::bestAsk() const noexcept {
    return m_engine.bestAsk();
}

template <class Sink>
std::uint64_t Venue<Sink>::trades() const noexcept {
    return m_nextMatch - 1;
}

template <class Sink>
std::size_t Venue<Sink>::liveOrders() const noexcept {
    return m_live.size();
}

template <class Sink>
bool Venue<Sink>::validPrice(std::uint64_t wire, Price& tick) const noexcept {
    if (wire % m_wirePerTick != 0) {
        return false;
    }
    const std::uint64_t t = wire / m_wirePerTick;
    if (t < static_cast<std::uint64_t>(m_minTick) || t > static_cast<std::uint64_t>(m_maxTick)) {
        return false;
    }
    tick = static_cast<Price>(t);
    return true;
}

template <class Sink>
bool Venue<Sink>::parseSide(char c, Side& side) const noexcept {
    switch (c) {
        case static_cast<char>(ouch::Side::Buy):
            side = Side::Buy;
            return true;
        case static_cast<char>(ouch::Side::Sell):
        case static_cast<char>(ouch::Side::SellShort):
        case static_cast<char>(ouch::Side::SellShortExempt):
            side = Side::Sell;
            return true;
        default:
            return false;
    }
}

template <class Sink>
void Venue<Sink>::processOrder(OrderId ref, Side side, Price tick, Quantity qty, std::uint64_t ts,
                               bool client, std::uint32_t user) {
    TradeEmitter em{this, ts, client, user};
    const Handle h = m_engine.add(ref, side, tick, qty, em);
    if (h != kNilHandle) {
        const Quantity rem = m_engine.order(h).qty;
        emitItchAdd(ref, side, tick, rem, ts);
        if (client) {
            trackClient(ref, h, side, tick, user);
        } else {
            m_live.insertOrAssign(ref, LiveOrder{h, side, tick, false, 0});
        }
    }
}

template <class Sink>
void Venue<Sink>::processImmediate(OrderId ref, Side side, Price tick, Quantity qty, std::uint64_t ts,
                                   std::uint32_t user) {
    TradeEmitter   em{this, ts, true, user};
    const Quantity rem = m_engine.match(ref, side, tick, qty, em);
    if (rem > 0) {
        emitCanceled(user, rem, ouch::CancelReason::Ioc, ts);
    }
}

template <class Sink>
void Venue<Sink>::handleTrade(const Trade& t, std::uint64_t ts, bool aggClient, std::uint32_t aggUser) {
    const std::uint64_t match = m_nextMatch++;
    emitItchExecuted(t.restingId, t.qty, match, ts);
    if (aggClient) {
        emitExecuted(aggUser, t.qty, t.price, 'R', match, ts);
    }
    const LiveOrder* live = m_live.find(t.restingId);
    if (live == nullptr) {
        return;
    }
    if (live->client) {
        emitExecuted(live->userRef, t.qty, t.price, 'A', match, ts);
        if (aggClient) {
            ++m_mirror.selfTrades;
        }
    } else if (aggClient) {
        ++m_mirror.impactFills;
    }
    if (t.restingFilled) {
        if (live->client) {
            dropClient(t.restingId, *live);
        } else {
            m_live.erase(t.restingId);
        }
    }
}

template <class Sink>
void Venue<Sink>::trackClient(OrderId ref, Handle h, Side side, Price tick, std::uint32_t user) {
    m_live.insertOrAssign(ref, LiveOrder{h, side, tick, true, user});
    m_byUserRef.insertOrAssign(user, ref);
    m_clientRefs.push_back(ref);
}

template <class Sink>
void Venue<Sink>::dropClient(OrderId ref, const LiveOrder& live) {
    m_byUserRef.erase(live.userRef);
    m_live.erase(ref);
    for (std::size_t i = 0; i < m_clientRefs.size(); ++i) {
        if (m_clientRefs[i] == ref) {
            m_clientRefs[i] = m_clientRefs.back();
            m_clientRefs.pop_back();
            return;
        }
    }
}

template <class Sink>
void Venue<Sink>::removeReal(OrderId ref, const LiveOrder& live) {
    m_engine.cancel(live.handle);
    m_live.erase(ref);
}

template <class Sink>
void Venue<Sink>::fillClient(OrderId ref, Quantity qty, std::uint64_t ts) {
    const LiveOrder* live = m_live.find(ref);
    if (live == nullptr || qty == 0) {
        return;
    }
    const LiveOrder     l     = *live;
    const std::uint64_t match = m_nextMatch++;
    emitItchExecuted(ref, qty, match, ts);
    emitExecuted(l.userRef, qty, l.tick, 'A', match, ts);
    const Quantity cur = m_engine.order(l.handle).qty;
    if (qty >= cur) {
        m_engine.cancel(l.handle);
        dropClient(ref, l);
    } else {
        m_engine.reduce(l.handle, cur - qty);
    }
}

template <class Sink>
Quantity Venue<Sink>::crossClients(Side side, Price tick, Quantity qty, std::uint64_t ts) {
    while (qty > 0) {
        OrderId best       = 0;
        Handle  bestHandle = kNilHandle;
        Price   bestTick   = 0;
        for (const OrderId ref : m_clientRefs) {
            const LiveOrder* l = m_live.find(ref);
            if (l == nullptr || l->side == side) {
                continue;
            }
            const bool crosses = side == Side::Buy ? l->tick <= tick : l->tick >= tick;
            if (!crosses) {
                continue;
            }
            const bool better = best == 0 || (side == Side::Buy ? l->tick < bestTick : l->tick > bestTick);
            if (better) {
                best       = ref;
                bestHandle = l->handle;
                bestTick   = l->tick;
            }
        }
        if (best == 0) {
            break;
        }
        const Quantity cur  = m_engine.order(bestHandle).qty;
        const Quantity fill = qty < cur ? qty : cur;
        ++m_mirror.crossFills;
        fillClient(best, fill, ts);
        qty -= fill;
    }
    return qty;
}

template <class Sink>
void Venue<Sink>::shadowFill(Side side, Price tick, OrderId realRef, Quantity qty, std::uint64_t ts) {
    for (std::size_t i = 0; i < m_clientRefs.size() && qty > 0;) {
        const OrderId    ref = m_clientRefs[i];
        const LiveOrder* l   = m_live.find(ref);
        if (l == nullptr || l->side != side) {
            ++i;
            continue;
        }
        const bool better = side == Side::Sell ? l->tick < tick : l->tick > tick;
        const bool ahead  = better || (l->tick == tick && aheadInLevel(side, tick, ref, realRef));
        if (!ahead) {
            ++i;
            continue;
        }
        const Quantity cur  = m_engine.order(l->handle).qty;
        const Quantity fill = qty < cur ? qty : cur;
        ++m_mirror.shadowFills;
        m_mirror.shadowShares += fill;
        fillClient(ref, fill, ts);
        qty -= fill;
        if (fill < cur) {
            ++i;
        }
    }
}

template <class Sink>
bool Venue<Sink>::aheadInLevel(Side side, Price tick, OrderId clientRef, OrderId realRef) const noexcept {
    bool ahead = false;
    m_engine.forEachOrderAtLevel(side, tick, [&](const Order& o) {
        if (o.id == clientRef) {
            ahead = true;
            return false;
        }
        if (o.id == realRef) {
            return false;
        }
        return true;
    });
    return ahead;
}

template <class Sink>
bool Venue<Sink>::isMarketable(Side side, Price tick) const noexcept {
    if (side == Side::Buy) {
        return m_engine.bestAsk() != kNoPrice && tick >= m_engine.bestAsk();
    }
    return m_engine.bestBid() != kNoPrice && tick <= m_engine.bestBid();
}

template <class Sink>
std::uint32_t Venue<Sink>::tickToWire(Price tick) const noexcept {
    return static_cast<std::uint32_t>(tick) * m_wirePerTick;
}

template <class Sink>
template <class Msg>
void Venue<Sink>::sendMd(const Msg& m) {
    m_sink.marketData({reinterpret_cast<const std::byte*>(&m), sizeof m});
}

template <class Sink>
template <class Msg>
void Venue<Sink>::sendOe(const Msg& m) {
    m_sink.orderEntry({reinterpret_cast<const std::byte*>(&m), sizeof m});
}

template <class Sink>
char Venue<Sink>::itchSide(Side s) noexcept {
    return s == Side::Buy ? static_cast<char>(itch::Side::Buy) : static_cast<char>(itch::Side::Sell);
}

template <class Sink>
void Venue<Sink>::emitItchAdd(OrderId ref, Side side, Price tick, Quantity shares, std::uint64_t ts) {
    itch::AddOrder a{};
    a.messageType    = static_cast<char>(itch::MessageType::AddOrder);
    a.stockLocate    = m_stockLocate;
    a.trackingNumber = 0;
    a.timestamp      = ts;
    a.orderRef       = ref;
    a.side           = itchSide(side);
    a.shares         = shares;
    a.stock          = std::string_view{m_symbol};
    a.price          = tickToWire(tick);
    sendMd(a);
}

template <class Sink>
void Venue<Sink>::emitItchExecuted(OrderId ref, Quantity shares, std::uint64_t match, std::uint64_t ts) {
    itch::OrderExecuted e{};
    e.messageType    = static_cast<char>(itch::MessageType::OrderExecuted);
    e.stockLocate    = m_stockLocate;
    e.trackingNumber = 0;
    e.timestamp      = ts;
    e.orderRef       = ref;
    e.executedShares = shares;
    e.matchNumber    = match;
    sendMd(e);
}

template <class Sink>
void Venue<Sink>::emitItchCancel(OrderId ref, Quantity shares, std::uint64_t ts) {
    itch::OrderCancel x{};
    x.messageType     = static_cast<char>(itch::MessageType::OrderCancel);
    x.stockLocate     = m_stockLocate;
    x.trackingNumber  = 0;
    x.timestamp       = ts;
    x.orderRef        = ref;
    x.cancelledShares = shares;
    sendMd(x);
}

template <class Sink>
void Venue<Sink>::emitItchDelete(OrderId ref, std::uint64_t ts) {
    itch::OrderDelete d{};
    d.messageType    = static_cast<char>(itch::MessageType::OrderDelete);
    d.stockLocate    = m_stockLocate;
    d.trackingNumber = 0;
    d.timestamp      = ts;
    d.orderRef       = ref;
    sendMd(d);
}

template <class Sink>
void Venue<Sink>::emitItchReplace(OrderId origRef, OrderId newRef, Quantity shares, Price tick,
                                  std::uint64_t ts) {
    itch::OrderReplace u{};
    u.messageType    = static_cast<char>(itch::MessageType::OrderReplace);
    u.stockLocate    = m_stockLocate;
    u.trackingNumber = 0;
    u.timestamp      = ts;
    u.origOrderRef   = origRef;
    u.newOrderRef    = newRef;
    u.shares         = shares;
    u.price          = tickToWire(tick);
    sendMd(u);
}

template <class Sink>
void Venue<Sink>::emitAccepted(const ouch::EnterOrder& o, OrderId ref, char orderState, std::uint64_t ts) {
    ouch::Accepted a{};
    a.type                 = static_cast<char>(ouch::OutType::Accepted);
    a.timestamp            = ts;
    a.userRefNum           = o.userRefNum.value();
    a.side                 = o.side;
    a.quantity             = o.quantity.value();
    a.symbol               = std::string_view{m_symbol};
    a.price                = o.price.value();
    a.timeInForce          = o.timeInForce;
    a.display              = o.display;
    a.orderReferenceNumber = ref;
    a.capacity             = o.capacity;
    a.imSweepEligibility   = o.imSweepEligibility;
    a.crossType            = o.crossType;
    a.orderState           = orderState;
    a.clOrdId              = o.clOrdId.view();
    a.appendageLength      = 0;
    sendOe(a);
}

template <class Sink>
void Venue<Sink>::emitExecuted(std::uint32_t user, Quantity shares, Price tick, char liq, std::uint64_t match,
                               std::uint64_t ts) {
    ouch::Executed e{};
    e.type            = static_cast<char>(ouch::OutType::Executed);
    e.timestamp       = ts;
    e.userRefNum      = user;
    e.quantity        = shares;
    e.price           = static_cast<std::uint64_t>(tickToWire(tick));
    e.liquidityFlag   = liq;
    e.matchNumber     = match;
    e.appendageLength = 0;
    sendOe(e);
}

template <class Sink>
void Venue<Sink>::emitCanceled(std::uint32_t user, Quantity decremented, ouch::CancelReason r,
                               std::uint64_t ts) {
    ouch::Canceled c{};
    c.type            = static_cast<char>(ouch::OutType::Canceled);
    c.timestamp       = ts;
    c.userRefNum      = user;
    c.quantity        = decremented;
    c.reason          = static_cast<char>(r);
    c.appendageLength = 0;
    sendOe(c);
}

template <class Sink>
void Venue<Sink>::emitReplaced(const ouch::ReplaceOrder& u, OrderId newRef, Side side, std::uint64_t ts) {
    ouch::Replaced r{};
    r.type                 = static_cast<char>(ouch::OutType::Replaced);
    r.timestamp            = ts;
    r.origUserRefNum       = u.origUserRefNum.value();
    r.userRefNum           = u.userRefNum.value();
    r.side                 = itchSide(side);
    r.quantity             = u.quantity.value();
    r.symbol               = std::string_view{m_symbol};
    r.price                = u.price.value();
    r.timeInForce          = u.timeInForce;
    r.display              = u.display;
    r.orderReferenceNumber = newRef;
    r.capacity             = static_cast<char>(ouch::Capacity::Agency);
    r.imSweepEligibility   = u.imSweepEligibility;
    r.crossType            = static_cast<char>(ouch::CrossType::Continuous);
    r.orderState           = static_cast<char>(ouch::OrderState::Live);
    r.clOrdId              = u.clOrdId.view();
    r.appendageLength      = 0;
    sendOe(r);
}

template <class Sink>
void Venue<Sink>::emitRejected(std::uint32_t user, ouch::RejectReason reason, std::string_view clOrdId,
                               std::uint64_t ts) {
    ouch::Rejected j{};
    j.type            = static_cast<char>(ouch::OutType::Rejected);
    j.timestamp       = ts;
    j.userRefNum      = user;
    j.reason          = static_cast<std::uint16_t>(reason);
    j.clOrdId         = clOrdId;
    j.appendageLength = 0;
    sendOe(j);
}

template <class Sink>
void Venue<Sink>::emitCancelReject(std::uint32_t user, std::uint64_t ts) {
    ouch::CancelReject i{};
    i.type            = static_cast<char>(ouch::OutType::CancelReject);
    i.timestamp       = ts;
    i.userRefNum      = user;
    i.appendageLength = 0;
    sendOe(i);
}

}   // namespace abt
