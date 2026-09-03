#include "t2t/dut/OrderManager.hpp"

#include <cstring>
#include <string_view>

namespace abt::dut {

namespace {

[[nodiscard]] std::size_t idx(Side side) noexcept {
    return side == Side::Buy ? 0u : 1u;
}

[[nodiscard]] ouch::Side ouchSide(Side side) noexcept {
    return side == Side::Buy ? ouch::Side::Buy : ouch::Side::Sell;
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
      m_nextUserRef(cfg.firstUserRef == 0 ? 1u : cfg.firstUserRef),
      m_slots(cfg.symbols.empty() ? 1 : cfg.symbols.size()),
      m_acct(cfg.symbols.empty() ? 1 : cfg.symbols.size()) {
    if (m_cfg.symbols.empty()) {
        m_cfg.symbols.emplace_back();
    }
    m_enter.reserve(m_cfg.symbols.size());
    for (const std::string& name : m_cfg.symbols) {
        m_enter.push_back(
            EnterTemplates{.bid = enterTemplate(name, Side::Buy), .ask = enterTemplate(name, Side::Sell)});
    }
    m_replace = replaceTemplate();
}

ouch::EnterOrder OrderManager::enterTemplate(std::string_view symbol, Side side) noexcept {
    ouch::EnterOrder o{};
    o.type               = ouch::InType::EnterOrder;
    o.userRefNum         = 0;
    o.side               = ouchSide(side);
    o.quantity           = 0;
    o.symbol             = symbol;
    o.price              = 0;
    o.timeInForce        = ouch::TimeInForce::Day;
    o.display            = ouch::Display::Visible;
    o.capacity           = ouch::Capacity::Principal;
    o.imSweepEligibility = ouch::ImSweep::NotEligible;
    o.crossType          = ouch::CrossType::Continuous;
    o.clOrdId            = std::string_view{};
    o.appendageLength    = 0;
    return o;
}

ouch::ReplaceOrder OrderManager::replaceTemplate() noexcept {
    ouch::ReplaceOrder u{};
    u.type               = ouch::InType::ReplaceOrder;
    u.origUserRefNum     = 0;
    u.userRefNum         = 0;
    u.quantity           = 0;
    u.price              = 0;
    u.timeInForce        = ouch::TimeInForce::Day;
    u.display            = ouch::Display::Visible;
    u.imSweepEligibility = ouch::ImSweep::NotEligible;
    u.clOrdId            = std::string_view{};
    u.appendageLength    = 0;
    return u;
}

void OrderManager::prefetch(std::size_t sym) const noexcept {
    __builtin_prefetch(&m_slots[sym][0]);
    __builtin_prefetch(&m_slots[sym][1]);
    __builtin_prefetch(&m_acct[sym]);
    __builtin_prefetch(&m_enter[sym]);
    __builtin_prefetch(&m_enter[sym].ask);
    __builtin_prefetch(&m_replace);
    __builtin_prefetch(&m_stats);
    __builtin_prefetch(&m_refs[m_nextUserRef % kRefRing]);
}

std::size_t OrderManager::reconcile(std::size_t sym, const QuoteTargets& t,
                                    std::span<Outbound, kMaxOutbound> out) noexcept {
    std::size_t      n         = 0;
    const QuoteSlot& ask       = m_slots[sym][idx(Side::Sell)];
    const bool       sellFirst = t.quoteBid && ask.state != QuoteState::Idle && t.bidPrice >= ask.price;
    if (sellFirst) {
        n += reconcileSide(sym, Side::Sell, t.quoteAsk, t.askPrice, t.askQty, out[n]);
        n += reconcileSide(sym, Side::Buy, t.quoteBid, t.bidPrice, t.bidQty, out[n]);
    } else {
        n += reconcileSide(sym, Side::Buy, t.quoteBid, t.bidPrice, t.bidQty, out[n]);
        n += reconcileSide(sym, Side::Sell, t.quoteAsk, t.askPrice, t.askQty, out[n]);
    }
    return n;
}

std::size_t OrderManager::reconcileSide(std::size_t sym, Side side, bool want, Price price, Quantity qty,
                                        Outbound& out) noexcept {
    return reconcileSlot(m_slots[sym][idx(side)], sym, side, want, price, qty, out);
}

void OrderManager::warmReconcile(std::size_t sym, Outbound& out) noexcept {
    QuoteSlot           scratch{};
    const OmsStats      stats   = m_stats;
    const std::uint32_t nextRef = m_nextUserRef;
    const RefSide       ref0    = m_refs[nextRef % kRefRing];
    const RefSide       ref1    = m_refs[(nextRef + 1) % kRefRing];
    (void)reconcileSlot(scratch, sym, Side::Buy, true, 1, 1, out);
    scratch.state  = QuoteState::Live;
    scratch.price  = 1;
    scratch.leaves = 1;
    (void)reconcileSlot(scratch, sym, Side::Buy, true, 2, 1, out);
    m_stats                          = stats;
    m_nextUserRef                    = nextRef;
    m_refs[nextRef % kRefRing]       = ref0;
    m_refs[(nextRef + 1) % kRefRing] = ref1;
}

std::size_t OrderManager::reconcileSlot(QuoteSlot& s, std::size_t sym, Side side, bool want, Price price,
                                        Quantity qty, Outbound& out) noexcept {
    if (want && qty == 0) {
        want = false;
    }
    switch (s.state) {
        case QuoteState::Idle: {
            if (!want) {
                return 0;
            }
            const std::uint32_t ref = allocRef(sym, side);
            encodeEnter(out, sym, ref, side, price, qty);
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
                ++m_stats.unchanged;
                return 0;
            }
            const std::uint32_t ref = allocRef(sym, side);
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
            m_stats.pendingSkips += want ? 1u : 0u;
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

std::size_t OrderManager::symbolCount() const noexcept {
    return m_slots.size();
}

const Account& OrderManager::account(std::size_t sym) const noexcept {
    return m_acct[sym];
}

std::int64_t OrderManager::netPosition() const noexcept {
    std::int64_t p = 0;
    for (const Account& a : m_acct) {
        p += a.position;
    }
    return p;
}

const QuoteSlot& OrderManager::slot(std::size_t sym, Side side) const noexcept {
    return m_slots[sym][idx(side)];
}

const QuoteSlot& OrderManager::slot(Side side) const noexcept {
    return m_slots[0][idx(side)];
}

const OmsStats& OrderManager::stats() const noexcept {
    return m_stats;
}

std::uint32_t OrderManager::nextUserRef() const noexcept {
    return m_nextUserRef;
}

std::uint32_t OrderManager::allocRef(std::size_t sym, Side side) noexcept {
    const std::uint32_t ref = m_nextUserRef++;
    if (m_nextUserRef == 0) {
        m_nextUserRef = 1;
    }
    m_refs[ref % kRefRing] = RefSide{.userRef = ref, .sym = static_cast<std::uint16_t>(sym), .side = side};
    return ref;
}

bool OrderManager::lookupRef(std::uint32_t userRef, std::size_t& sym, Side& side) const noexcept {
    const RefSide& r = m_refs[userRef % kRefRing];
    if (r.userRef != userRef) {
        return false;
    }
    sym  = r.sym;
    side = r.side;
    return true;
}

bool OrderManager::isTestRef(std::uint32_t userRef) const noexcept {
    const RefSide& r = m_refs[userRef % kRefRing];
    return r.userRef == userRef && r.sym == kTestSym;
}

void OrderManager::warmEncode(std::size_t sym, Outbound& out) const noexcept {
    encodeEnter(out, sym, 0, Side::Buy, 0, 0);
    encodeEnter(out, sym, 0, Side::Sell, 0, 0);
    encodeReplace(out, 0, 0, 0, 0);
}

void OrderManager::encodeTestOrder(Outbound& out) noexcept {
    const std::uint32_t ref = allocRef(kTestSym, Side::Buy);
    ouch::EnterOrder    o{};
    o.type               = ouch::InType::EnterOrder;
    o.userRefNum         = ref;
    o.side               = ouchSide(Side::Buy);
    o.quantity           = 1;
    o.symbol             = kTestSymbol;
    o.price              = wirePrice(100);
    o.timeInForce        = ouch::TimeInForce::Day;
    o.display            = ouch::Display::Visible;
    o.capacity           = ouch::Capacity::Principal;
    o.imSweepEligibility = ouch::ImSweep::NotEligible;
    o.crossType          = ouch::CrossType::Continuous;
    o.clOrdId            = std::string_view{};
    o.appendageLength    = 0;
    std::memcpy(out.buf.data(), &o, sizeof o);
    out.len     = sizeof o;
    out.userRef = ref;
}

QuoteSlot* OrderManager::slotByRef(std::uint32_t userRef, std::size_t& sym) noexcept {
    Side side = Side::Buy;
    if (std::size_t hint = 0; lookupRef(userRef, hint, side) && hint < m_slots.size()) {
        QuoteSlot& s = m_slots[hint][idx(side)];
        if (s.state != QuoteState::Idle && s.userRef == userRef) {
            sym = hint;
            return &s;
        }
    }
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        for (QuoteSlot& s : m_slots[i]) {
            if (s.state != QuoteState::Idle && s.userRef == userRef) {
                sym = i;
                return &s;
            }
        }
    }
    return nullptr;
}

QuoteSlot* OrderManager::slotByPending(std::uint32_t userRef) noexcept {
    for (Pair& pair : m_slots) {
        for (QuoteSlot& s : pair) {
            if (s.state == QuoteState::PendingReplace && s.pendingRef == userRef) {
                return &s;
            }
        }
    }
    return nullptr;
}

void OrderManager::encodeEnter(Outbound& out, std::size_t sym, std::uint32_t userRef, Side side, Price price,
                               Quantity qty) const noexcept {
    ouch::EnterOrder o = side == Side::Buy ? m_enter[sym].bid : m_enter[sym].ask;
    o.userRefNum       = userRef;
    o.quantity         = qty;
    o.price            = wirePrice(price);
    std::memcpy(out.buf.data(), &o, sizeof o);
    out.len     = sizeof o;
    out.userRef = userRef;
}

void OrderManager::encodeReplace(Outbound& out, std::uint32_t origRef, std::uint32_t userRef, Price price,
                                 Quantity qty) const noexcept {
    ouch::ReplaceOrder u = m_replace;
    u.origUserRefNum     = origRef;
    u.userRefNum         = userRef;
    u.quantity           = qty;
    u.price              = wirePrice(price);
    std::memcpy(out.buf.data(), &u, sizeof u);
    out.len     = sizeof u;
    out.userRef = userRef;
}

void OrderManager::encodeCancel(Outbound& out, std::uint32_t userRef) noexcept {
    ouch::CancelOrder x{};
    x.type            = ouch::InType::CancelOrder;
    x.userRefNum      = userRef;
    x.quantity        = 0;
    x.appendageLength = 0;
    std::memcpy(out.buf.data(), &x, sizeof x);
    out.len     = sizeof x;
    out.userRef = userRef;
}

void OrderManager::onAccepted(const ouch::Accepted& m) noexcept {
    if (isTestRef(m.userRefNum.value())) [[unlikely]] {
        ++m_stats.tests;
        return;
    }
    std::size_t sym = 0;
    QuoteSlot*  s   = slotByRef(m.userRefNum.value(), sym);
    if (s == nullptr || s->state != QuoteState::PendingNew) {
        ++m_stats.unknown;
        return;
    }
    ++m_stats.accepts;
    s->qty    = m.quantity.value();
    s->leaves = s->qty;
    if (m.orderState == ouch::OrderState::Dead) {
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
    if (m.orderState == ouch::OrderState::Dead) {
        s->leaves = 0;
        settle(*s);
    }
}

void OrderManager::onExecuted(const ouch::Executed& m) noexcept {
    const std::uint32_t ref  = m.userRefNum.value();
    const Quantity      qty  = m.quantity.value();
    Side                side = Side::Buy;
    std::size_t         sym  = 0;
    if (!lookupRef(ref, sym, side)) {
        ++m_stats.unknown;
        return;
    }
    ++m_stats.fills;
    if (side == Side::Buy) {
        m_acct[sym].position += static_cast<std::int64_t>(qty);
    } else {
        m_acct[sym].position -= static_cast<std::int64_t>(qty);
    }
    QuoteSlot* s = slotByRef(ref, sym);
    if (s == nullptr) {
        return;
    }
    s->leaves = qty >= s->leaves ? 0 : s->leaves - qty;
    if (s->leaves == 0 && s->state == QuoteState::Live) {
        settle(*s);
    }
}

void OrderManager::onCanceled(const ouch::Canceled& m) noexcept {
    std::size_t sym = 0;
    QuoteSlot*  s   = slotByRef(m.userRefNum.value(), sym);
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
    if (isTestRef(ref)) [[unlikely]] {
        ++m_stats.tests;
        return;
    }
    ++m_stats.rejects;
    switch (static_cast<ouch::RejectReason>(m.reason.value())) {
        case ouch::RejectReason::ReplaceNotAllowed:
            ++m_stats.rejReplace;
            break;
        case ouch::RejectReason::InvalidPrice:
            ++m_stats.rejPrice;
            break;
        case ouch::RejectReason::InvalidQuantity:
            ++m_stats.rejQty;
            break;
        default:
            ++m_stats.rejOther;
            break;
    }
    if (QuoteSlot* p = slotByPending(ref); p != nullptr) {
        p->pendingRef = 0;
        if (p->leaves > 0) {
            p->state = QuoteState::Live;
        } else {
            settle(*p);
        }
        return;
    }
    std::size_t sym = 0;
    QuoteSlot*  s   = slotByRef(ref, sym);
    if (s == nullptr) {
        ++m_stats.unknown;
        return;
    }
    s->leaves = 0;
    settle(*s);
}

void OrderManager::onCancelReject(const ouch::CancelReject& m) noexcept {
    std::size_t sym = 0;
    QuoteSlot*  s   = slotByRef(m.userRefNum.value(), sym);
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
