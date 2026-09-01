#include "t2t/dut/OrderManager.hpp"

#include <cstring>
#include <string_view>

namespace abt::dut {

namespace {

[[nodiscard]] std::size_t idx(Side side) noexcept {
    return side == Side::Buy ? 0u : 1u;
}

[[nodiscard]] char ouchSide(Side side) noexcept {
    return side == Side::Buy ? static_cast<char>(ouch::Side::Buy) : static_cast<char>(ouch::Side::Sell);
}

template <class M>
[[nodiscard]] bool read(std::span<const std::byte> bytes, M& out) noexcept {
    if (bytes.size() < sizeof(M)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(M));
    return true;
}

}   // namespace

OrderManager::OrderManager(const OmsConfig& cfg)
    : m_cfg(cfg),
      m_nextUserRef(cfg.firstUserRef == 0 ? 1u : cfg.firstUserRef) {
}

std::size_t OrderManager::reconcile(const QuoteTargets& t, std::span<Outbound, kMaxOutbound> out) noexcept {
    std::size_t      n         = 0;
    const QuoteSlot& ask       = m_slots[idx(Side::Sell)];
    const bool       sellFirst = t.quoteBid && ask.state != QuoteState::Idle && t.bidPrice >= ask.price;
    if (sellFirst) {
        n += reconcileSide(Side::Sell, t.quoteAsk, t.askPrice, t.askQty, out[n]);
        n += reconcileSide(Side::Buy, t.quoteBid, t.bidPrice, t.bidQty, out[n]);
    } else {
        n += reconcileSide(Side::Buy, t.quoteBid, t.bidPrice, t.bidQty, out[n]);
        n += reconcileSide(Side::Sell, t.quoteAsk, t.askPrice, t.askQty, out[n]);
    }
    return n;
}

std::size_t OrderManager::reconcileSide(Side side, bool want, Price price, Quantity qty,
                                        Outbound& out) noexcept {
    QuoteSlot& s = m_slots[idx(side)];
    if (want && qty == 0) {
        want = false;
    }
    switch (s.state) {
        case QuoteState::Idle: {
            if (!want) {
                return 0;
            }
            const std::uint32_t ref = allocRef(side);
            encodeEnter(out, ref, side, price, qty);
            s.state      = QuoteState::PendingNew;
            s.userRef    = ref;
            s.pendingRef = 0;
            s.price      = price;
            s.qty        = qty;
            s.leaves     = qty;
            ++m_stats.enters;
            return 1;
        }
        case QuoteState::Live: {
            if (!want) {
                encodeCancel(out, s.userRef);
                s.state = QuoteState::PendingCancel;
                ++m_stats.cancels;
                return 1;
            }
            if (price == s.price && qty == s.leaves) {
                return 0;
            }
            const std::uint32_t ref = allocRef(side);
            encodeReplace(out, s.userRef, ref, price, qty);
            s.state      = QuoteState::PendingReplace;
            s.pendingRef = ref;
            ++m_stats.replaces;
            return 1;
        }
        case QuoteState::PendingNew:
        case QuoteState::PendingReplace:
        case QuoteState::PendingCancel:
        default: {
            return 0;
        }
    }
}

void OrderManager::onAck(std::span<const std::byte> ouch) noexcept {
    if (ouch.empty()) {
        return;
    }
    switch (static_cast<ouch::OutType>(static_cast<char>(ouch[0]))) {
        case ouch::OutType::Accepted: {
            ouch::Accepted m{};
            if (read(ouch, m)) {
                onAccepted(m);
            }
            break;
        }
        case ouch::OutType::Replaced: {
            ouch::Replaced m{};
            if (read(ouch, m)) {
                onReplaced(m);
            }
            break;
        }
        case ouch::OutType::Executed: {
            ouch::Executed m{};
            if (read(ouch, m)) {
                onExecuted(m);
            }
            break;
        }
        case ouch::OutType::Canceled: {
            ouch::Canceled m{};
            if (read(ouch, m)) {
                onCanceled(m);
            }
            break;
        }
        case ouch::OutType::Rejected: {
            ouch::Rejected m{};
            if (read(ouch, m)) {
                onRejected(m);
            }
            break;
        }
        case ouch::OutType::CancelReject: {
            ouch::CancelReject m{};
            if (read(ouch, m)) {
                onCancelReject(m);
            }
            break;
        }
        default: {
            break;
        }
    }
}

const Account& OrderManager::account() const noexcept {
    return m_acct;
}

const QuoteSlot& OrderManager::slot(Side side) const noexcept {
    return m_slots[idx(side)];
}

const OmsStats& OrderManager::stats() const noexcept {
    return m_stats;
}

std::uint32_t OrderManager::nextUserRef() const noexcept {
    return m_nextUserRef;
}

std::uint32_t OrderManager::allocRef(Side side) noexcept {
    const std::uint32_t ref = m_nextUserRef++;
    if (m_nextUserRef == 0) {
        m_nextUserRef = 1;
    }
    m_refs[ref % kRefRing] = RefSide{.userRef = ref, .side = side};
    return ref;
}

bool OrderManager::sideOf(std::uint32_t userRef, Side& side) const noexcept {
    const RefSide& r = m_refs[userRef % kRefRing];
    if (r.userRef != userRef) {
        return false;
    }
    side = r.side;
    return true;
}

QuoteSlot* OrderManager::slotByRef(std::uint32_t userRef) noexcept {
    for (QuoteSlot& s : m_slots) {
        if (s.state != QuoteState::Idle && s.userRef == userRef) {
            return &s;
        }
    }
    return nullptr;
}

QuoteSlot* OrderManager::slotByPending(std::uint32_t userRef) noexcept {
    for (QuoteSlot& s : m_slots) {
        if (s.state == QuoteState::PendingReplace && s.pendingRef == userRef) {
            return &s;
        }
    }
    return nullptr;
}

void OrderManager::encodeEnter(Outbound& out, std::uint32_t userRef, Side side, Price price,
                               Quantity qty) const noexcept {
    ouch::EnterOrder o{};
    o.type               = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum         = userRef;
    o.side               = ouchSide(side);
    o.quantity           = qty;
    o.symbol             = std::string_view{m_cfg.symbol};
    o.price              = wirePrice(price);
    o.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    o.display            = static_cast<char>(ouch::Display::Visible);
    o.capacity           = static_cast<char>(ouch::Capacity::Principal);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType          = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId            = std::string_view{};
    o.appendageLength    = 0;
    std::memcpy(out.buf.data(), &o, sizeof o);
    out.len     = sizeof o;
    out.userRef = userRef;
}

void OrderManager::encodeReplace(Outbound& out, std::uint32_t origRef, std::uint32_t userRef, Price price,
                                 Quantity qty) const noexcept {
    ouch::ReplaceOrder u{};
    u.type               = static_cast<char>(ouch::InType::ReplaceOrder);
    u.origUserRefNum     = origRef;
    u.userRefNum         = userRef;
    u.quantity           = qty;
    u.price              = wirePrice(price);
    u.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    u.display            = static_cast<char>(ouch::Display::Visible);
    u.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    u.clOrdId            = std::string_view{};
    u.appendageLength    = 0;
    std::memcpy(out.buf.data(), &u, sizeof u);
    out.len     = sizeof u;
    out.userRef = userRef;
}

void OrderManager::encodeCancel(Outbound& out, std::uint32_t userRef) const noexcept {
    ouch::CancelOrder x{};
    x.type            = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum      = userRef;
    x.quantity        = 0;
    x.appendageLength = 0;
    std::memcpy(out.buf.data(), &x, sizeof x);
    out.len     = sizeof x;
    out.userRef = userRef;
}

void OrderManager::onAccepted(const ouch::Accepted& m) noexcept {
    QuoteSlot* s = slotByRef(m.userRefNum.value());
    if (s == nullptr || s->state != QuoteState::PendingNew) {
        ++m_stats.unknown;
        return;
    }
    ++m_stats.accepts;
    s->qty    = m.quantity.value();
    s->leaves = s->qty;
    if (m.orderState == static_cast<char>(ouch::OrderState::Dead)) {
        s->leaves = 0;
        settle(*s);
        return;
    }
    s->state = QuoteState::Live;
}

void OrderManager::onReplaced(const ouch::Replaced& m) noexcept {
    QuoteSlot* s = slotByPending(m.userRefNum.value());
    if (s == nullptr) {
        ++m_stats.unknown;
        return;
    }
    ++m_stats.accepts;
    s->userRef    = m.userRefNum.value();
    s->pendingRef = 0;
    s->price      = static_cast<Price>(m.price.value());
    s->qty        = m.quantity.value();
    s->leaves     = s->qty;
    s->state      = QuoteState::Live;
    if (m.orderState == static_cast<char>(ouch::OrderState::Dead)) {
        s->leaves = 0;
        settle(*s);
    }
}

void OrderManager::onExecuted(const ouch::Executed& m) noexcept {
    const std::uint32_t ref  = m.userRefNum.value();
    const Quantity      qty  = m.quantity.value();
    Side                side = Side::Buy;
    if (!sideOf(ref, side)) {
        ++m_stats.unknown;
        return;
    }
    ++m_stats.fills;
    if (side == Side::Buy) {
        m_acct.position += static_cast<std::int64_t>(qty);
    } else {
        m_acct.position -= static_cast<std::int64_t>(qty);
    }
    QuoteSlot* s = slotByRef(ref);
    if (s == nullptr) {
        return;
    }
    s->leaves = qty >= s->leaves ? 0 : s->leaves - qty;
    if (s->leaves == 0 && s->state == QuoteState::Live) {
        settle(*s);
    }
}

void OrderManager::onCanceled(const ouch::Canceled& m) noexcept {
    QuoteSlot* s = slotByRef(m.userRefNum.value());
    if (s == nullptr) {
        ++m_stats.unknown;
        return;
    }
    const Quantity qty = m.quantity.value();
    s->leaves          = qty >= s->leaves ? 0 : s->leaves - qty;
    if (s->leaves == 0 || s->state == QuoteState::PendingCancel) {
        s->leaves = 0;
        settle(*s);
        return;
    }
    if (s->state == QuoteState::PendingNew) {
        s->state = QuoteState::Live;
    }
}

void OrderManager::onRejected(const ouch::Rejected& m) noexcept {
    const std::uint32_t ref = m.userRefNum.value();
    ++m_stats.rejects;
    if (QuoteSlot* p = slotByPending(ref); p != nullptr) {
        p->pendingRef = 0;
        if (p->leaves > 0) {
            p->state = QuoteState::Live;
        } else {
            settle(*p);
        }
        return;
    }
    QuoteSlot* s = slotByRef(ref);
    if (s == nullptr) {
        ++m_stats.unknown;
        return;
    }
    s->leaves = 0;
    settle(*s);
}

void OrderManager::onCancelReject(const ouch::CancelReject& m) noexcept {
    QuoteSlot* s = slotByRef(m.userRefNum.value());
    if (s == nullptr || s->state != QuoteState::PendingCancel) {
        ++m_stats.unknown;
        return;
    }
    if (s->leaves > 0) {
        s->state = QuoteState::Live;
    } else {
        settle(*s);
    }
}

void OrderManager::settle(QuoteSlot& s) noexcept {
    s.state      = QuoteState::Idle;
    s.userRef    = 0;
    s.pendingRef = 0;
    s.price      = 0;
    s.qty        = 0;
    s.leaves     = 0;
}

}   // namespace abt::dut
