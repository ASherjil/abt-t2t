#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "t2t/dut/OrderManager.hpp"
#include "t2t/protocol/Ouch50.hpp"

using namespace abt;
using dut::QuoteState;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& msg) {
    return {reinterpret_cast<const std::byte*>(&msg), sizeof msg};
}

template <class M>
M decode(const dut::Outbound& o) {
    M m{};
    std::memcpy(&m, o.buf.data(), sizeof m);
    return m;
}

dut::OrderManager makeOms() {
    dut::OmsConfig cfg{};
    cfg.symbol       = "ABT";
    cfg.firstUserRef = 1;
    return dut::OrderManager(cfg);
}

dut::QuoteTargets both(Price bid, Price ask, Quantity qty = 100) {
    dut::QuoteTargets t{};
    t.quoteBid = true;
    t.bidPrice = bid;
    t.bidQty   = qty;
    t.quoteAsk = true;
    t.askPrice = ask;
    t.askQty   = qty;
    return t;
}

ouch::Accepted accepted(std::uint32_t ref, Quantity qty, ouch::OrderState state = ouch::OrderState::Live) {
    ouch::Accepted a{};
    a.type       = ouch::OutType::Accepted;
    a.userRefNum = ref;
    a.quantity   = qty;
    a.orderState = state;
    return a;
}

ouch::Replaced replaced(std::uint32_t orig, std::uint32_t ref, Quantity qty, Price price) {
    ouch::Replaced r{};
    r.type           = ouch::OutType::Replaced;
    r.origUserRefNum = orig;
    r.userRefNum     = ref;
    r.quantity       = qty;
    r.price          = wirePrice(price);
    r.orderState     = ouch::OrderState::Live;
    return r;
}

ouch::Executed executed(std::uint32_t ref, Quantity qty) {
    ouch::Executed e{};
    e.type       = ouch::OutType::Executed;
    e.userRefNum = ref;
    e.quantity   = qty;
    return e;
}

ouch::Canceled canceled(std::uint32_t ref, Quantity qty) {
    ouch::Canceled c{};
    c.type       = ouch::OutType::Canceled;
    c.userRefNum = ref;
    c.quantity   = qty;
    c.reason     = ouch::CancelReason::UserRequested;
    return c;
}

ouch::Rejected rejected(std::uint32_t ref) {
    ouch::Rejected j{};
    j.type       = ouch::OutType::Rejected;
    j.userRefNum = ref;
    return j;
}

ouch::CancelReject cancelReject(std::uint32_t ref) {
    ouch::CancelReject i{};
    i.type       = ouch::OutType::CancelReject;
    i.userRefNum = ref;
    return i;
}

void test_enter_both_sides_then_idle_while_pending() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};

    CHECK_EQ(oms.reconcile(both(99, 101), out), 2u);
    const auto b = decode<ouch::EnterOrder>(out[0]);
    const auto a = decode<ouch::EnterOrder>(out[1]);
    CHECK_EQ(out[0].len, sizeof(ouch::EnterOrder));
    CHECK(b.side == ouch::Side::Buy);
    CHECK_EQ(b.userRefNum.value(), 1u);
    CHECK_EQ(b.price.value(), 99u);
    CHECK_EQ(b.quantity.value(), 100u);
    CHECK(b.symbol.view() == "ABT");
    CHECK(b.capacity == ouch::Capacity::Principal);
    CHECK(a.side == ouch::Side::Sell);
    CHECK_EQ(a.userRefNum.value(), 2u);
    CHECK_EQ(a.price.value(), 101u);
    CHECK(oms.slot(Side::Buy).state == QuoteState::PendingNew);
    CHECK(oms.slot(Side::Sell).state == QuoteState::PendingNew);

    CHECK_EQ(oms.reconcile(both(98, 102), out), 0u);

    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));
    CHECK(oms.slot(Side::Buy).state == QuoteState::Live);
    CHECK(oms.slot(Side::Sell).state == QuoteState::Live);
    CHECK_EQ(oms.slot(Side::Buy).leaves, 100u);
    CHECK_EQ(oms.stats().accepts, 2u);

    CHECK_EQ(oms.reconcile(both(99, 101), out), 0u);
}

void test_replace_on_price_move() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));

    CHECK_EQ(oms.reconcile(both(98, 101), out), 1u);
    const auto u = decode<ouch::ReplaceOrder>(out[0]);
    CHECK_EQ(out[0].len, sizeof(ouch::ReplaceOrder));
    CHECK_EQ(u.origUserRefNum.value(), 1u);
    CHECK_EQ(u.userRefNum.value(), 3u);
    CHECK_EQ(u.price.value(), 98u);
    CHECK(oms.slot(Side::Buy).state == QuoteState::PendingReplace);
    CHECK_EQ(oms.slot(Side::Buy).pendingRef, 3u);

    CHECK_EQ(oms.reconcile(both(97, 101), out), 0u);

    oms.onAck(bytesOf(replaced(1, 3, 100, 98)));
    CHECK(oms.slot(Side::Buy).state == QuoteState::Live);
    CHECK_EQ(oms.slot(Side::Buy).userRef, 3u);
    CHECK_EQ(oms.slot(Side::Buy).price, 98);
    CHECK_EQ(oms.stats().replaces, 1u);
}

void test_pull_side_cancels() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));

    dut::QuoteTargets t = both(99, 101);
    t.quoteAsk          = false;
    CHECK_EQ(oms.reconcile(t, out), 1u);
    const auto x = decode<ouch::CancelOrder>(out[0]);
    CHECK_EQ(out[0].len, sizeof(ouch::CancelOrder));
    CHECK_EQ(x.userRefNum.value(), 2u);
    CHECK_EQ(x.quantity.value(), 0u);
    CHECK(oms.slot(Side::Sell).state == QuoteState::PendingCancel);

    oms.onAck(bytesOf(canceled(2, 100)));
    CHECK(oms.slot(Side::Sell).state == QuoteState::Idle);

    CHECK_EQ(oms.reconcile(both(99, 101), out), 1u);
    CHECK_EQ(decode<ouch::EnterOrder>(out[0]).userRefNum.value(), 3u);
}

void test_fills_drive_position_and_requote() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));

    oms.onAck(bytesOf(executed(1, 40)));
    CHECK_EQ(oms.account().position, 40);
    CHECK_EQ(oms.slot(Side::Buy).leaves, 60u);
    CHECK(oms.slot(Side::Buy).state == QuoteState::Live);

    CHECK_EQ(oms.reconcile(both(99, 101), out), 1u);
    const auto u = decode<ouch::ReplaceOrder>(out[0]);
    CHECK_EQ(u.origUserRefNum.value(), 1u);
    CHECK_EQ(u.quantity.value(), 100u);
    oms.onAck(bytesOf(replaced(1, 3, 100, 99)));

    oms.onAck(bytesOf(executed(2, 100)));
    CHECK_EQ(oms.account().position, -60);
    CHECK(oms.slot(Side::Sell).state == QuoteState::Idle);
    CHECK_EQ(oms.stats().fills, 2u);

    oms.onAck(bytesOf(executed(3, 100)));
    CHECK_EQ(oms.account().position, 40);
    CHECK(oms.slot(Side::Buy).state == QuoteState::Idle);
}

void test_fill_during_pending_replace() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));
    (void)oms.reconcile(both(98, 101), out);
    CHECK(oms.slot(Side::Buy).state == QuoteState::PendingReplace);

    oms.onAck(bytesOf(executed(1, 100)));
    CHECK_EQ(oms.account().position, 100);
    CHECK(oms.slot(Side::Buy).state == QuoteState::PendingReplace);

    oms.onAck(bytesOf(rejected(3)));
    CHECK(oms.slot(Side::Buy).state == QuoteState::Idle);
}

void test_rejects() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(rejected(1)));
    CHECK(oms.slot(Side::Buy).state == QuoteState::Idle);
    CHECK_EQ(oms.stats().rejects, 1u);
    oms.onAck(bytesOf(accepted(2, 100)));

    (void)oms.reconcile(both(99, 102), out);
    CHECK(oms.slot(Side::Sell).state == QuoteState::PendingReplace);
    oms.onAck(bytesOf(rejected(4)));
    CHECK(oms.slot(Side::Sell).state == QuoteState::Live);
    CHECK_EQ(oms.slot(Side::Sell).userRef, 2u);
    CHECK_EQ(oms.slot(Side::Sell).price, 101);

    const dut::QuoteTargets t{};
    (void)oms.reconcile(t, out);
    CHECK(oms.slot(Side::Sell).state == QuoteState::PendingCancel);
    oms.onAck(bytesOf(cancelReject(2)));
    CHECK(oms.slot(Side::Sell).state == QuoteState::Live);

    oms.onAck(bytesOf(accepted(999, 5)));
    CHECK_EQ(oms.stats().unknown, 1u);
}

void test_ioc_dead_on_accept() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    dut::QuoteTargets            t{};
    t.quoteBid = true;
    t.bidPrice = 101;
    t.bidQty   = 10;
    (void)oms.reconcile(t, out);
    oms.onAck(bytesOf(accepted(1, 10, ouch::OrderState::Dead)));
    CHECK(oms.slot(Side::Buy).state == QuoteState::Idle);
}

}   // namespace

void test_rising_quote_moves_ask_before_bid() {
    auto                         oms = makeOms();
    std::array<dut::Outbound, 2> out{};
    (void)oms.reconcile(both(99, 101), out);
    oms.onAck(bytesOf(accepted(1, 100)));
    oms.onAck(bytesOf(accepted(2, 100)));

    CHECK_EQ(oms.reconcile(both(101, 103), out), 2u);
    const auto first  = decode<ouch::ReplaceOrder>(out[0]);
    const auto second = decode<ouch::ReplaceOrder>(out[1]);
    CHECK_EQ(first.origUserRefNum.value(), 2u);
    CHECK_EQ(first.price.value(), 103u);
    CHECK_EQ(second.origUserRefNum.value(), 1u);
    CHECK_EQ(second.price.value(), 101u);
    oms.onAck(bytesOf(replaced(2, 3, 100, 103)));
    oms.onAck(bytesOf(replaced(1, 4, 100, 101)));

    CHECK_EQ(oms.reconcile(both(97, 99), out), 2u);
    const auto b = decode<ouch::ReplaceOrder>(out[0]);
    const auto a = decode<ouch::ReplaceOrder>(out[1]);
    CHECK_EQ(b.origUserRefNum.value(), 4u);
    CHECK_EQ(b.price.value(), 97u);
    CHECK_EQ(a.origUserRefNum.value(), 3u);
    CHECK_EQ(a.price.value(), 99u);
}

int main() {
    test_rising_quote_moves_ask_before_bid();
    test_enter_both_sides_then_idle_while_pending();
    test_replace_on_price_move();
    test_pull_side_cancels();
    test_fills_drive_position_and_requote();
    test_fill_during_pending_replace();
    test_rejects();
    test_ioc_dead_on_accept();
    return abt::test::summary("oms");
}
