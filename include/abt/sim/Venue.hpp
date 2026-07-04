#pragma once
//
// Exchange venue: OUCH order entry <-> matching engine <-> ITCH/OUCH output.
//

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "abt/lob/OrderBook.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/Ouch50.hpp"

namespace abt {

template <class Sink>
class Venue {
public:
    Venue(Sink& sink, std::string_view symbol, std::uint16_t stockLocate,
          Price minTick, Price maxTick, std::uint32_t wirePerTick = 100);

    void sessionEvent(itch::SystemEventCode code, std::uint64_t ts);
    void onEnterOrder(const ouch::EnterOrder& o, std::uint64_t ts);
    void onCancelOrder(const ouch::CancelOrder& x, std::uint64_t ts);
    void onReplaceOrder(const ouch::ReplaceOrder& u, std::uint64_t ts);

    OrderId injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts);
    void cancelSynthetic(OrderId ref, std::uint64_t ts);

    [[nodiscard]] const OrderBook& book() const noexcept;
    [[nodiscard]] Price bestBid() const noexcept;
    [[nodiscard]] Price bestAsk() const noexcept;

private:
    struct LiveOrder {
        Handle        handle;
        Side          side;
        Price         tick;
        bool          client;
        std::uint32_t userRef;
    };

    struct TradeEmitter {
        Venue*        v;
        std::uint64_t ts;
        bool          aggClient;
        std::uint32_t aggUser;
        void onTrade(const Trade& t) { v->handleTrade(t, ts, aggClient, aggUser); }
    };
    friend struct TradeEmitter;

    void processOrder(OrderId ref, Side side, Price tick, Quantity qty, std::uint64_t ts,
                      bool client, std::uint32_t user);
    void handleTrade(const Trade& t, std::uint64_t ts, bool aggClient, std::uint32_t aggUser);
    [[nodiscard]] bool isMarketable(Side side, Price tick) const noexcept;
    [[nodiscard]] Price priceToTick(std::uint64_t wire) const noexcept;
    [[nodiscard]] std::uint32_t tickToWire(Price tick) const noexcept;

    template <class Msg> void sendMd(const Msg& m);
    template <class Msg> void sendOe(const Msg& m);

    static char itchSide(Side s) noexcept;

    void emitItchAdd(OrderId ref, Side side, Price tick, Quantity shares, std::uint64_t ts);
    void emitItchExecuted(OrderId ref, Quantity shares, std::uint64_t match, std::uint64_t ts);
    void emitItchCancel(OrderId ref, Quantity shares, std::uint64_t ts);
    void emitItchDelete(OrderId ref, std::uint64_t ts);
    void emitItchReplace(OrderId origRef, OrderId newRef, Quantity shares, Price tick,
                         std::uint64_t ts);
    void emitAccepted(const ouch::EnterOrder& o, OrderId ref, char orderState, std::uint64_t ts);
    void emitExecuted(std::uint32_t user, Quantity shares, Price tick, char liq,
                      std::uint64_t match, std::uint64_t ts);
    void emitCanceled(std::uint32_t user, Quantity decremented, ouch::CancelReason r,
                      std::uint64_t ts);
    void emitReplaced(const ouch::ReplaceOrder& u, OrderId origRef, OrderId newRef, Side side,
                      std::uint64_t ts);

    Sink&         m_sink;
    std::string   m_symbol;
    std::uint16_t m_stockLocate;
    std::uint32_t m_wirePerTick;
    OrderBook     m_engine;

    OrderId       m_nextOrderRef = 1;
    std::uint64_t m_nextMatch    = 1;

    std::unordered_map<OrderId, LiveOrder>     m_live;
    std::unordered_map<std::uint32_t, OrderId> m_byUserRef;
};

template <class Sink>
Venue<Sink>::Venue(Sink& sink, std::string_view symbol, std::uint16_t stockLocate,
                   Price minTick, Price maxTick, std::uint32_t wirePerTick)
    : m_sink(sink),
      m_symbol(symbol),
      m_stockLocate(stockLocate),
      m_wirePerTick(wirePerTick),
      m_engine(minTick, maxTick) {}

template <class Sink>
void Venue<Sink>::sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
    itch::SystemEvent s{};
    s.messageType = static_cast<char>(itch::MessageType::SystemEvent);
    s.stockLocate = 0;
    s.trackingNumber = 0;
    s.timestamp = ts;
    s.eventCode = static_cast<char>(code);
    sendMd(s);
}

template <class Sink>
void Venue<Sink>::onEnterOrder(const ouch::EnterOrder& o, std::uint64_t ts) {
    const OrderId ref = m_nextOrderRef++;
    const std::uint32_t user = o.userRefNum.value();
    const Side side = (o.side == static_cast<char>(ouch::Side::Buy)) ? Side::Buy : Side::Sell;
    const Price tick = priceToTick(o.price.value());

    emitAccepted(o, ref, static_cast<char>(ouch::OrderState::Live), ts);
    processOrder(ref, side, tick, o.quantity.value(), ts, true, user);
}

template <class Sink>
void Venue<Sink>::onCancelOrder(const ouch::CancelOrder& x, std::uint64_t ts) {
    const std::uint32_t user = x.userRefNum.value();
    const auto uit = m_byUserRef.find(user);
    if (uit == m_byUserRef.end()) return;
    const OrderId ref = uit->second;
    const auto lit = m_live.find(ref);
    if (lit == m_live.end()) return;

    const Handle h = lit->second.handle;
    const Quantity cur = m_engine.order(h).qty;
    const Quantity intended = x.quantity.value();
    if (intended >= cur) return;

    if (intended == 0) {
        const Quantity removed = m_engine.cancel(h);
        emitItchDelete(ref, ts);
        emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
        m_byUserRef.erase(uit);
        m_live.erase(lit);
    } else {
        const Quantity removed = m_engine.reduce(h, intended);
        emitItchCancel(ref, removed, ts);
        emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
    }
}

template <class Sink>
void Venue<Sink>::onReplaceOrder(const ouch::ReplaceOrder& u, std::uint64_t ts) {
    const std::uint32_t origUser = u.origUserRefNum.value();
    const auto uit = m_byUserRef.find(origUser);
    if (uit == m_byUserRef.end()) return;
    const OrderId origRef = uit->second;
    const auto lit = m_live.find(origRef);
    if (lit == m_live.end()) return;

    const LiveOrder orig = lit->second;
    const std::uint32_t newUser = u.userRefNum.value();
    const Price tick = priceToTick(u.price.value());
    const Quantity qty = u.quantity.value();
    const Side side = orig.side;

    m_engine.cancel(orig.handle);
    m_byUserRef.erase(uit);
    m_live.erase(lit);

    const OrderId newRef = m_nextOrderRef++;
    emitReplaced(u, origRef, newRef, side, ts);

    if (isMarketable(side, tick)) {
        emitItchDelete(origRef, ts);
        processOrder(newRef, side, tick, qty, ts, true, newUser);
    } else {
        const Handle h = m_engine.add(newRef, side, tick, qty);
        emitItchReplace(origRef, newRef, qty, tick, ts);
        if (h != kNilHandle) {
            m_live.emplace(newRef, LiveOrder{h, side, tick, true, newUser});
            m_byUserRef[newUser] = newRef;
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
    const auto lit = m_live.find(ref);
    if (lit == m_live.end()) return;
    m_engine.cancel(lit->second.handle);
    emitItchDelete(ref, ts);
    m_live.erase(lit);
}

template <class Sink>
const OrderBook& Venue<Sink>::book() const noexcept { return m_engine; }
template <class Sink>
Price Venue<Sink>::bestBid() const noexcept { return m_engine.bestBid(); }
template <class Sink>
Price Venue<Sink>::bestAsk() const noexcept { return m_engine.bestAsk(); }

template <class Sink>
void Venue<Sink>::processOrder(OrderId ref, Side side, Price tick, Quantity qty,
                               std::uint64_t ts, bool client, std::uint32_t user) {
    TradeEmitter em{this, ts, client, user};
    const Handle h = m_engine.add(ref, side, tick, qty, em);
    if (h != kNilHandle) {
        const Quantity rem = m_engine.order(h).qty;
        emitItchAdd(ref, side, tick, rem, ts);
        m_live.emplace(ref, LiveOrder{h, side, tick, client, user});
        if (client) m_byUserRef[user] = ref;
    }
}

template <class Sink>
void Venue<Sink>::handleTrade(const Trade& t, std::uint64_t ts, bool aggClient,
                              std::uint32_t aggUser) {
    const std::uint64_t match = m_nextMatch++;
    emitItchExecuted(t.restingId, t.qty, match, ts);
    if (aggClient) {
        emitExecuted(aggUser, t.qty, t.price, 'R', match, ts);
    }
    const auto it = m_live.find(t.restingId);
    if (it != m_live.end()) {
        if (it->second.client) {
            emitExecuted(it->second.userRef, t.qty, t.price, 'A', match, ts);
        }
        if (t.restingFilled) {
            if (it->second.client) m_byUserRef.erase(it->second.userRef);
            m_live.erase(it);
        }
    }
}

template <class Sink>
bool Venue<Sink>::isMarketable(Side side, Price tick) const noexcept {
    if (side == Side::Buy)
        return m_engine.bestAsk() != kNoPrice && tick >= m_engine.bestAsk();
    return m_engine.bestBid() != kNoPrice && tick <= m_engine.bestBid();
}

template <class Sink>
Price Venue<Sink>::priceToTick(std::uint64_t wire) const noexcept {
    return static_cast<Price>(wire / m_wirePerTick);
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
    return s == Side::Buy ? static_cast<char>(itch::Side::Buy)
                          : static_cast<char>(itch::Side::Sell);
}

template <class Sink>
void Venue<Sink>::emitItchAdd(OrderId ref, Side side, Price tick, Quantity shares,
                              std::uint64_t ts) {
    itch::AddOrder a{};
    a.messageType = static_cast<char>(itch::MessageType::AddOrder);
    a.stockLocate = m_stockLocate;
    a.trackingNumber = 0;
    a.timestamp = ts;
    a.orderRef = ref;
    a.side = itchSide(side);
    a.shares = shares;
    a.stock = std::string_view{m_symbol};
    a.price = tickToWire(tick);
    sendMd(a);
}

template <class Sink>
void Venue<Sink>::emitItchExecuted(OrderId ref, Quantity shares, std::uint64_t match,
                                   std::uint64_t ts) {
    itch::OrderExecuted e{};
    e.messageType = static_cast<char>(itch::MessageType::OrderExecuted);
    e.stockLocate = m_stockLocate;
    e.trackingNumber = 0;
    e.timestamp = ts;
    e.orderRef = ref;
    e.executedShares = shares;
    e.matchNumber = match;
    sendMd(e);
}

template <class Sink>
void Venue<Sink>::emitItchCancel(OrderId ref, Quantity shares, std::uint64_t ts) {
    itch::OrderCancel x{};
    x.messageType = static_cast<char>(itch::MessageType::OrderCancel);
    x.stockLocate = m_stockLocate;
    x.trackingNumber = 0;
    x.timestamp = ts;
    x.orderRef = ref;
    x.cancelledShares = shares;
    sendMd(x);
}

template <class Sink>
void Venue<Sink>::emitItchDelete(OrderId ref, std::uint64_t ts) {
    itch::OrderDelete d{};
    d.messageType = static_cast<char>(itch::MessageType::OrderDelete);
    d.stockLocate = m_stockLocate;
    d.trackingNumber = 0;
    d.timestamp = ts;
    d.orderRef = ref;
    sendMd(d);
}

template <class Sink>
void Venue<Sink>::emitItchReplace(OrderId origRef, OrderId newRef, Quantity shares, Price tick,
                                  std::uint64_t ts) {
    itch::OrderReplace u{};
    u.messageType = static_cast<char>(itch::MessageType::OrderReplace);
    u.stockLocate = m_stockLocate;
    u.trackingNumber = 0;
    u.timestamp = ts;
    u.origOrderRef = origRef;
    u.newOrderRef = newRef;
    u.shares = shares;
    u.price = tickToWire(tick);
    sendMd(u);
}

template <class Sink>
void Venue<Sink>::emitAccepted(const ouch::EnterOrder& o, OrderId ref, char orderState,
                               std::uint64_t ts) {
    ouch::Accepted a{};
    a.type = static_cast<char>(ouch::OutType::Accepted);
    a.timestamp = ts;
    a.userRefNum = o.userRefNum.value();
    a.side = o.side;
    a.quantity = o.quantity.value();
    a.symbol = std::string_view{m_symbol};
    a.price = o.price.value();
    a.timeInForce = o.timeInForce;
    a.display = o.display;
    a.orderReferenceNumber = ref;
    a.capacity = o.capacity;
    a.imSweepEligibility = o.imSweepEligibility;
    a.crossType = o.crossType;
    a.orderState = orderState;
    a.clOrdId = o.clOrdId.view();
    a.appendageLength = 0;
    sendOe(a);
}

template <class Sink>
void Venue<Sink>::emitExecuted(std::uint32_t user, Quantity shares, Price tick, char liq,
                               std::uint64_t match, std::uint64_t ts) {
    ouch::Executed e{};
    e.type = static_cast<char>(ouch::OutType::Executed);
    e.timestamp = ts;
    e.userRefNum = user;
    e.quantity = shares;
    e.price = static_cast<std::uint64_t>(tickToWire(tick));
    e.liquidityFlag = liq;
    e.matchNumber = match;
    e.appendageLength = 0;
    sendOe(e);
}

template <class Sink>
void Venue<Sink>::emitCanceled(std::uint32_t user, Quantity decremented, ouch::CancelReason r,
                               std::uint64_t ts) {
    ouch::Canceled c{};
    c.type = static_cast<char>(ouch::OutType::Canceled);
    c.timestamp = ts;
    c.userRefNum = user;
    c.quantity = decremented;
    c.reason = static_cast<char>(r);
    c.appendageLength = 0;
    sendOe(c);
}

template <class Sink>
void Venue<Sink>::emitReplaced(const ouch::ReplaceOrder& u, OrderId origRef, OrderId newRef,
                               Side side, std::uint64_t ts) {
    ouch::Replaced r{};
    r.type = static_cast<char>(ouch::OutType::Replaced);
    r.timestamp = ts;
    r.origUserRefNum = u.origUserRefNum.value();
    r.userRefNum = u.userRefNum.value();
    r.side = itchSide(side);
    r.quantity = u.quantity.value();
    r.symbol = std::string_view{m_symbol};
    r.price = u.price.value();
    r.timeInForce = u.timeInForce;
    r.display = u.display;
    r.orderReferenceNumber = newRef;
    r.capacity = static_cast<char>(ouch::Capacity::Agency);
    r.imSweepEligibility = u.imSweepEligibility;
    r.crossType = static_cast<char>(ouch::CrossType::Continuous);
    r.orderState = static_cast<char>(ouch::OrderState::Live);
    r.clOrdId = u.clOrdId.view();
    r.appendageLength = 0;
    sendOe(r);
    (void)origRef;
}

}
