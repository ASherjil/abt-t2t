#pragma once
//
// Venue.hpp -- the exchange simulator's core behaviour: OUCH order entry <-> matching
// engine <-> ITCH market data + OUCH execution reports.
//
// Responsibilities:
//   * Decode-driven: callers hand it decoded inbound OUCH (EnterOrder / CancelOrder /
//     ReplaceOrder) plus an ingress timestamp (ns since midnight -- in the real rig this
//     comes from the NIC RX hardware timestamp).
//   * Assigns exchange Order Reference Numbers and Match Numbers.
//   * Runs the matching engine and translates the result into wire messages:
//       - ITCH market data (public feed): AddOrder / OrderExecuted / OrderCancel /
//         OrderDelete / OrderReplace / SystemEvent.
//       - OUCH order entry (to the order's owner): Accepted / Executed / Canceled /
//         Replaced.
//   * Synthetic orders (from the flow generator) go through injectSynthetic(): they
//     populate the book and appear on the ITCH feed, but generate no OUCH (no owner).
//
// The Sink template parameter is the output boundary; it must provide:
//     void marketData(std::span<const std::byte>);   // one ITCH message body
//     void orderEntry(std::span<const std::byte>);    // one OUCH message body
// Framing (MoldUDP64 / SoupBinTCP) and transport wrap these; here we emit bodies only.
//
// Scope of this first cut (documented simplifications):
//   * Single symbol, single order-entry client. Multi-symbol/-client is a later refinement.
//   * Plain limit semantics: Time-in-Force (IOC/GTX/...) and option appendages (peg,
//     MinQty, ...) are accepted but not yet enforced.
//   * Prices are penny-aligned: book tick = wire Price / wirePerTick (default 100 = $0.01).
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

namespace abt::sim {

template <class Sink>
class Venue {
public:
    Venue(Sink& sink, std::string_view symbol, std::uint16_t stockLocate,
          lob::Price minTick, lob::Price maxTick, std::uint32_t wirePerTick = 100)
        : m_sink(sink),
          m_symbol(symbol),
          m_stockLocate(stockLocate),
          m_wirePerTick(wirePerTick),
          m_engine(minTick, maxTick) {}

    // ---- session lifecycle ---------------------------------------------------------
    void sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
        itch::SystemEvent s{};
        s.messageType = static_cast<char>(itch::MessageType::SystemEvent);
        s.stockLocate = 0;
        s.trackingNumber = 0;
        s.timestamp = ts;
        s.eventCode = static_cast<char>(code);
        sendMd(s);
    }

    // ---- inbound OUCH (from the order-entry client) --------------------------------
    void onEnterOrder(const ouch::EnterOrder& o, std::uint64_t ts) {
        const lob::OrderId ref = m_nextOrderRef++;
        const std::uint32_t user = o.userRefNum.value();
        const lob::Side side = (o.side == static_cast<char>(ouch::Side::Buy))
                                   ? lob::Side::Buy : lob::Side::Sell;
        const lob::Price tick = priceToTick(o.price.value());

        emitAccepted(o, ref, static_cast<char>(ouch::OrderState::Live), ts);
        processOrder(ref, side, tick, o.quantity.value(), ts, /*client=*/true, user);
    }

    void onCancelOrder(const ouch::CancelOrder& x, std::uint64_t ts) {
        const std::uint32_t user = x.userRefNum.value();
        const auto uit = m_byUserRef.find(user);
        if (uit == m_byUserRef.end()) return;              // unknown -> silently ignore
        const lob::OrderId ref = uit->second;
        const auto lit = m_live.find(ref);
        if (lit == m_live.end()) return;

        const lob::Handle h = lit->second.handle;
        const lob::Quantity cur = m_engine.order(h).qty;
        const lob::Quantity intended = x.quantity.value();  // new intended order size
        if (intended >= cur) return;                        // no reduction

        if (intended == 0) {
            const lob::Quantity removed = m_engine.cancel(h);
            emitItchDelete(ref, ts);
            emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
            m_byUserRef.erase(uit);
            m_live.erase(lit);
        } else {
            const lob::Quantity removed = m_engine.reduce(h, intended);
            emitItchCancel(ref, removed, ts);               // ITCH 'X' partial cancel
            emitCanceled(user, removed, ouch::CancelReason::UserRequested, ts);
        }
    }

    void onReplaceOrder(const ouch::ReplaceOrder& u, std::uint64_t ts) {
        const std::uint32_t origUser = u.origUserRefNum.value();
        const auto uit = m_byUserRef.find(origUser);
        if (uit == m_byUserRef.end()) return;               // orig not live -> ignore
        const lob::OrderId origRef = uit->second;
        const auto lit = m_live.find(origRef);
        if (lit == m_live.end()) return;

        const LiveOrder orig = lit->second;                 // copy before erasing
        const std::uint32_t newUser = u.userRefNum.value();
        const lob::Price tick = priceToTick(u.price.value());
        const lob::Quantity qty = u.quantity.value();
        const lob::Side side = orig.side;                   // replace keeps side

        // Remove the original from the engine and our maps (replace loses time priority).
        m_engine.cancel(orig.handle);
        m_byUserRef.erase(uit);
        m_live.erase(lit);

        const lob::OrderId newRef = m_nextOrderRef++;
        emitReplaced(u, origRef, newRef, side, ts);

        if (isMarketable(side, tick)) {
            // The replacement crosses: delete-then-add semantics on the ITCH feed.
            emitItchDelete(origRef, ts);
            processOrder(newRef, side, tick, qty, ts, /*client=*/true, newUser);
        } else {
            // Non-crossing: one atomic ITCH OrderReplace conveys cancel-orig + add-new.
            const lob::Handle h = m_engine.add(newRef, side, tick, qty);
            emitItchReplace(origRef, newRef, qty, tick, ts);
            if (h != lob::kNilHandle) {
                m_live.emplace(newRef, LiveOrder{h, side, tick, true, newUser});
                m_byUserRef[newUser] = newRef;
            }
        }
    }

    // ---- synthetic order flow (from the generator; ITCH only, no OUCH) -------------
    lob::OrderId injectSynthetic(lob::Side side, lob::Price tick, lob::Quantity qty,
                                 std::uint64_t ts) {
        const lob::OrderId ref = m_nextOrderRef++;
        processOrder(ref, side, tick, qty, ts, /*client=*/false, 0);
        return ref;
    }
    void cancelSynthetic(lob::OrderId ref, std::uint64_t ts) {
        const auto lit = m_live.find(ref);
        if (lit == m_live.end()) return;
        m_engine.cancel(lit->second.handle);
        emitItchDelete(ref, ts);
        m_live.erase(lit);
    }

    // ---- accessors -----------------------------------------------------------------
    [[nodiscard]] const lob::OrderBook& book() const noexcept { return m_engine; }
    [[nodiscard]] lob::Price bestBid() const noexcept { return m_engine.bestBid(); }
    [[nodiscard]] lob::Price bestAsk() const noexcept { return m_engine.bestAsk(); }

private:
    struct LiveOrder {
        lob::Handle   handle;
        lob::Side     side;
        lob::Price    tick;
        bool          client;
        std::uint32_t userRef;   // valid iff client
    };

    // Sink invoked by the matching engine per fill (aggressor-side context bound in).
    struct TradeEmitter {
        Venue*        v;
        std::uint64_t ts;
        bool          aggClient;
        std::uint32_t aggUser;
        void onTrade(const lob::Trade& t) { v->handleTrade(t, ts, aggClient, aggUser); }
    };
    friend struct TradeEmitter;

    // Match an incoming order, emit ITCH/OUCH for fills, rest+register any remainder.
    void processOrder(lob::OrderId ref, lob::Side side, lob::Price tick,
                      lob::Quantity qty, std::uint64_t ts, bool client,
                      std::uint32_t user) {
        TradeEmitter em{this, ts, client, user};
        const lob::Handle h = m_engine.add(ref, side, tick, qty, em);
        if (h != lob::kNilHandle) {
            const lob::Quantity rem = m_engine.order(h).qty;
            emitItchAdd(ref, side, tick, rem, ts);
            m_live.emplace(ref, LiveOrder{h, side, tick, client, user});
            if (client) m_byUserRef[user] = ref;
        }
    }

    void handleTrade(const lob::Trade& t, std::uint64_t ts, bool aggClient,
                     std::uint32_t aggUser) {
        const std::uint64_t match = m_nextMatch++;
        emitItchExecuted(t.restingId, t.qty, match, ts);
        if (aggClient) {
            emitExecuted(aggUser, t.qty, t.price, 'R', match, ts);   // removed liquidity
        }
        const auto it = m_live.find(t.restingId);
        if (it != m_live.end()) {
            if (it->second.client) {
                emitExecuted(it->second.userRef, t.qty, t.price, 'A', match, ts);  // added
            }
            if (t.restingFilled) {
                if (it->second.client) m_byUserRef.erase(it->second.userRef);
                m_live.erase(it);
            }
        }
    }

    [[nodiscard]] bool isMarketable(lob::Side side, lob::Price tick) const noexcept {
        if (side == lob::Side::Buy)
            return m_engine.bestAsk() != lob::kNoPrice && tick >= m_engine.bestAsk();
        return m_engine.bestBid() != lob::kNoPrice && tick <= m_engine.bestBid();
    }

    // --- price conversion (book ticks <-> wire Price(4/8) integer) -------------------
    [[nodiscard]] lob::Price priceToTick(std::uint64_t wire) const noexcept {
        return static_cast<lob::Price>(wire / m_wirePerTick);
    }
    [[nodiscard]] std::uint32_t tickToWire(lob::Price tick) const noexcept {
        return static_cast<std::uint32_t>(tick) * m_wirePerTick;
    }

    // --- message emission ------------------------------------------------------------
    template <class Msg> void sendMd(const Msg& m) {
        m_sink.marketData({reinterpret_cast<const std::byte*>(&m), sizeof m});
    }
    template <class Msg> void sendOe(const Msg& m) {
        m_sink.orderEntry({reinterpret_cast<const std::byte*>(&m), sizeof m});
    }

    static char itchSide(lob::Side s) noexcept {
        return s == lob::Side::Buy ? static_cast<char>(itch::Side::Buy)
                                   : static_cast<char>(itch::Side::Sell);
    }

    void emitItchAdd(lob::OrderId ref, lob::Side side, lob::Price tick,
                     lob::Quantity shares, std::uint64_t ts) {
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
    void emitItchExecuted(lob::OrderId ref, lob::Quantity shares, std::uint64_t match,
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
    void emitItchCancel(lob::OrderId ref, lob::Quantity shares, std::uint64_t ts) {
        itch::OrderCancel x{};
        x.messageType = static_cast<char>(itch::MessageType::OrderCancel);
        x.stockLocate = m_stockLocate;
        x.trackingNumber = 0;
        x.timestamp = ts;
        x.orderRef = ref;
        x.cancelledShares = shares;
        sendMd(x);
    }
    void emitItchDelete(lob::OrderId ref, std::uint64_t ts) {
        itch::OrderDelete d{};
        d.messageType = static_cast<char>(itch::MessageType::OrderDelete);
        d.stockLocate = m_stockLocate;
        d.trackingNumber = 0;
        d.timestamp = ts;
        d.orderRef = ref;
        sendMd(d);
    }
    void emitItchReplace(lob::OrderId origRef, lob::OrderId newRef, lob::Quantity shares,
                         lob::Price tick, std::uint64_t ts) {
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

    void emitAccepted(const ouch::EnterOrder& o, lob::OrderId ref, char orderState,
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
    void emitExecuted(std::uint32_t user, lob::Quantity shares, lob::Price tick, char liq,
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
    void emitCanceled(std::uint32_t user, lob::Quantity decremented, ouch::CancelReason r,
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
    void emitReplaced(const ouch::ReplaceOrder& u, lob::OrderId origRef, lob::OrderId newRef,
                      lob::Side side, std::uint64_t ts) {
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

    Sink&         m_sink;
    std::string   m_symbol;
    std::uint16_t m_stockLocate;
    std::uint32_t m_wirePerTick;
    lob::OrderBook m_engine;

    lob::OrderId  m_nextOrderRef = 1;
    std::uint64_t m_nextMatch    = 1;

    std::unordered_map<lob::OrderId, LiveOrder>   m_live;       // orderRef -> live order
    std::unordered_map<std::uint32_t, lob::OrderId> m_byUserRef; // client userRef -> orderRef
};

}  // namespace abt::sim
