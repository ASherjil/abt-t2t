//
// Verifies the venue turns OUCH orders into the correct ITCH + OUCH message flows.
//

#include "TestHarness.hpp"

#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/sim/Venue.hpp"

using namespace abt;

namespace {

struct RecSink {
    std::vector<std::vector<std::byte>> md;
    std::vector<std::vector<std::byte>> oe;

    void marketData(std::span<const std::byte> b) {
        md.emplace_back(b.begin(), b.end());
    }

    void orderEntry(std::span<const std::byte> b) {
        oe.emplace_back(b.begin(), b.end());
    }

    void clear() {
        md.clear();
        oe.clear();
    }
};

char type_of(const std::vector<std::byte>& v) {
    return static_cast<char>(v[0]);
}

template <class M>
M decode(const std::vector<std::byte>& v) {
    M m{};
    std::memcpy(&m, v.data(), sizeof m);
    return m;
}

ouch::EnterOrder makeEnter(std::uint32_t user, char side, std::uint32_t qty, std::string_view sym,
                           std::uint64_t price) {
    ouch::EnterOrder o{};
    o.type               = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum         = user;
    o.side               = side;
    o.quantity           = qty;
    o.symbol             = sym;
    o.price              = price;
    o.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    o.display            = static_cast<char>(ouch::Display::Visible);
    o.capacity           = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType          = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId            = std::string_view{"CID"};
    o.appendageLength    = 0;
    return o;
}

constexpr std::uint64_t kPx52 = 520000;
constexpr std::uint64_t kPx51 = 510000;

void test_enter_rests() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);

    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000'000);

    CHECK_EQ(sink.oe.size(), 1u);
    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'A');
    CHECK(type_of(sink.md[0]) == 'A');

    const auto acc = decode<ouch::Accepted>(sink.oe[0]);
    CHECK_EQ(acc.userRefNum.value(), 1000u);
    CHECK_EQ(acc.orderReferenceNumber.value(), 1u);
    CHECK_EQ(acc.quantity.value(), 100u);
    CHECK_EQ(acc.price.value(), kPx52);
    CHECK(acc.orderState == static_cast<char>(ouch::OrderState::Live));

    const auto add = decode<itch::AddOrder>(sink.md[0]);
    CHECK_EQ(add.orderRef.value(), 1u);
    CHECK(add.side == 'B');
    CHECK_EQ(add.shares.value(), 100u);
    CHECK_EQ(add.price.value(), 520000u);
    CHECK(add.stock.view() == "AAPL");
    CHECK_EQ(v.bestBid(), 5200);
}

void test_cross_against_synthetic() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);

    const auto synthRef = v.injectSynthetic(Side::Sell, 5200, 100, 1'000);
    CHECK_EQ(synthRef, 1u);
    CHECK_EQ(sink.md.size(), 1u);
    CHECK_EQ(sink.oe.size(), 0u);
    sink.clear();

    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 2'000);

    CHECK_EQ(sink.oe.size(), 2u);
    CHECK(type_of(sink.oe[0]) == 'A');
    CHECK(type_of(sink.oe[1]) == 'E');
    const auto exe = decode<ouch::Executed>(sink.oe[1]);
    CHECK_EQ(exe.userRefNum.value(), 1000u);
    CHECK_EQ(exe.quantity.value(), 100u);
    CHECK_EQ(exe.price.value(), 520000u);
    CHECK_EQ(exe.matchNumber.value(), 1u);
    CHECK(exe.liquidityFlag == 'R');

    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.md[0]) == 'E');
    const auto ie = decode<itch::OrderExecuted>(sink.md[0]);
    CHECK_EQ(ie.orderRef.value(), 1u);
    CHECK_EQ(ie.executedShares.value(), 100u);
    CHECK_EQ(ie.matchNumber.value(), 1u);
    CHECK_EQ(v.bestAsk(), kNoPrice);
}

void test_full_cancel() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::CancelOrder x{};
    x.type            = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum      = 1000u;
    x.quantity        = 0u;
    x.appendageLength = 0;
    v.onCancelOrder(x, 2'000);

    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.md[0]) == 'D');
    CHECK_EQ(decode<itch::OrderDelete>(sink.md[0]).orderRef.value(), 1u);

    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'C');
    const auto c = decode<ouch::Canceled>(sink.oe[0]);
    CHECK_EQ(c.userRefNum.value(), 1000u);
    CHECK_EQ(c.quantity.value(), 100u);
    CHECK_EQ(v.bestBid(), kNoPrice);
}

void test_partial_cancel() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::CancelOrder x{};
    x.type            = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum      = 1000u;
    x.quantity        = 30u;
    x.appendageLength = 0;
    v.onCancelOrder(x, 2'000);

    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.md[0]) == 'X');
    CHECK_EQ(decode<itch::OrderCancel>(sink.md[0]).cancelledShares.value(), 70u);
    CHECK_EQ(sink.oe.size(), 1u);
    CHECK_EQ(decode<ouch::Canceled>(sink.oe[0]).quantity.value(), 70u);
    CHECK_EQ(v.book().volumeAt(Side::Buy, 5200), 30u);
}

void test_replace_noncrossing() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::ReplaceOrder u{};
    u.type               = static_cast<char>(ouch::InType::ReplaceOrder);
    u.origUserRefNum     = 1000u;
    u.userRefNum         = 1001u;
    u.quantity           = 150u;
    u.price              = kPx51;
    u.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    u.display            = static_cast<char>(ouch::Display::Visible);
    u.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    u.clOrdId            = std::string_view{"CID2"};
    u.appendageLength    = 0;
    v.onReplaceOrder(u, 2'000);

    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'U');
    const auto rp = decode<ouch::Replaced>(sink.oe[0]);
    CHECK_EQ(rp.origUserRefNum.value(), 1000u);
    CHECK_EQ(rp.userRefNum.value(), 1001u);
    CHECK_EQ(rp.orderReferenceNumber.value(), 2u);
    CHECK_EQ(rp.quantity.value(), 150u);

    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.md[0]) == 'U');
    const auto iu = decode<itch::OrderReplace>(sink.md[0]);
    CHECK_EQ(iu.origOrderRef.value(), 1u);
    CHECK_EQ(iu.newOrderRef.value(), 2u);
    CHECK_EQ(iu.shares.value(), 150u);
    CHECK_EQ(iu.price.value(), 510000u);
    CHECK_EQ(v.bestBid(), 5100);
    CHECK_EQ(v.book().volumeAt(Side::Buy, 5200), 0u);
    CHECK_EQ(v.book().volumeAt(Side::Buy, 5100), 150u);
}

void test_reject_paths() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);

    v.onEnterOrder(makeEnter(1, 'B', 0, "AAPL", kPx52), 1'000);
    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'J');
    auto j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.userRefNum.value(), 1u);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::InvalidQuantity));
    CHECK(j.clOrdId.view() == "CID");
    sink.clear();

    v.onEnterOrder(makeEnter(2, 'B', 100, "MSFT", kPx52), 1'000);
    j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::InvalidSymbol));
    sink.clear();

    v.onEnterOrder(makeEnter(3, 'B', 100, "AAPL", 520050), 1'000);
    j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::InvalidPrice));
    sink.clear();

    v.onEnterOrder(makeEnter(4, 'B', 100, "AAPL", 100000000ull), 1'000);
    j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::InvalidPrice));
    sink.clear();

    v.onEnterOrder(makeEnter(5, 'Q', 100, "AAPL", kPx52), 1'000);
    j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::InvalidSide));
    CHECK_EQ(sink.md.size(), 0u);
    CHECK(v.book().empty());
}

void test_cancel_reject() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::CancelOrder x{};
    x.type            = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum      = 4242u;
    x.quantity        = 0u;
    x.appendageLength = 0;
    v.onCancelOrder(x, 2'000);
    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'I');
    CHECK_EQ(decode<ouch::CancelReject>(sink.oe[0]).userRefNum.value(), 4242u);
    CHECK_EQ(sink.md.size(), 0u);
    sink.clear();

    x.userRefNum = 1000u;
    x.quantity   = 100u;
    v.onCancelOrder(x, 2'000);
    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'I');
    CHECK_EQ(v.book().volumeAt(Side::Buy, 5200), 100u);
}

void test_replace_unknown_rejected() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);

    ouch::ReplaceOrder u{};
    u.type           = static_cast<char>(ouch::InType::ReplaceOrder);
    u.origUserRefNum = 77u;
    u.userRefNum     = 78u;
    u.quantity       = 10u;
    u.price          = kPx51;
    u.clOrdId        = std::string_view{"R1"};
    v.onReplaceOrder(u, 1'000);
    CHECK_EQ(sink.oe.size(), 1u);
    CHECK(type_of(sink.oe[0]) == 'J');
    const auto j = decode<ouch::Rejected>(sink.oe[0]);
    CHECK_EQ(j.userRefNum.value(), 78u);
    CHECK_EQ(j.reason.value(), static_cast<std::uint16_t>(ouch::RejectReason::ReplaceNotAllowed));
}

void test_ioc_partial_fill_cancels_rest() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.injectSynthetic(Side::Sell, 5200, 40, 1'000);
    sink.clear();

    auto o        = makeEnter(1000, 'B', 100, "AAPL", kPx52);
    o.timeInForce = static_cast<char>(ouch::TimeInForce::IOC);
    v.onEnterOrder(o, 2'000);

    CHECK_EQ(sink.oe.size(), 3u);
    CHECK(type_of(sink.oe[0]) == 'A');
    CHECK(decode<ouch::Accepted>(sink.oe[0]).orderState == static_cast<char>(ouch::OrderState::Dead));
    CHECK(type_of(sink.oe[1]) == 'E');
    CHECK_EQ(decode<ouch::Executed>(sink.oe[1]).quantity.value(), 40u);
    CHECK(type_of(sink.oe[2]) == 'C');
    const auto c = decode<ouch::Canceled>(sink.oe[2]);
    CHECK_EQ(c.quantity.value(), 60u);
    CHECK(c.reason == static_cast<char>(ouch::CancelReason::Ioc));

    CHECK_EQ(sink.md.size(), 1u);
    CHECK(type_of(sink.md[0]) == 'E');
    CHECK(v.book().empty());
}

void test_ioc_no_liquidity() {
    RecSink        sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);

    auto o        = makeEnter(1000, 'S', 100, "AAPL", kPx52);
    o.timeInForce = static_cast<char>(ouch::TimeInForce::IOC);
    v.onEnterOrder(o, 2'000);

    CHECK_EQ(sink.oe.size(), 2u);
    CHECK(type_of(sink.oe[0]) == 'A');
    CHECK(type_of(sink.oe[1]) == 'C');
    CHECK_EQ(decode<ouch::Canceled>(sink.oe[1]).quantity.value(), 100u);
    CHECK_EQ(sink.md.size(), 0u);
    CHECK(v.book().empty());
}

}   // namespace

int main() {
    test_enter_rests();
    test_cross_against_synthetic();
    test_full_cancel();
    test_partial_cancel();
    test_replace_noncrossing();
    test_reject_paths();
    test_cancel_reject();
    test_replace_unknown_rejected();
    test_ioc_partial_fill_cancels_rest();
    test_ioc_no_liquidity();
    return abt::test::summary("venue_test");
}
