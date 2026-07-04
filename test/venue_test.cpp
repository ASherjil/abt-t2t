//
// Verifies the venue turns OUCH orders into the correct ITCH + OUCH message flows.
//

#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/sim/Venue.hpp"
#include "TestHarness.hpp"

using namespace abt;

namespace {

struct RecSink {
    std::vector<std::vector<std::byte>> md;
    std::vector<std::vector<std::byte>> oe;
    void marketData(std::span<const std::byte> b) { md.emplace_back(b.begin(), b.end()); }
    void orderEntry(std::span<const std::byte> b) { oe.emplace_back(b.begin(), b.end()); }
    void clear() { md.clear(); oe.clear(); }
};

char type_of(const std::vector<std::byte>& v) { return static_cast<char>(v[0]); }

template <class M>
M decode(const std::vector<std::byte>& v) {
    M m{};
    std::memcpy(&m, v.data(), sizeof m);
    return m;
}

ouch::EnterOrder makeEnter(std::uint32_t user, char side, std::uint32_t qty,
                           std::string_view sym, std::uint64_t price) {
    ouch::EnterOrder o{};
    o.type = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum = user;
    o.side = side;
    o.quantity = qty;
    o.symbol = sym;
    o.price = price;
    o.timeInForce = static_cast<char>(ouch::TimeInForce::Day);
    o.display = static_cast<char>(ouch::Display::Visible);
    o.capacity = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId = std::string_view{"CID"};
    o.appendageLength = 0;
    return o;
}

constexpr std::uint64_t kPx52 = 520000;
constexpr std::uint64_t kPx51 = 510000;

void test_enter_rests() {
    RecSink sink;
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
    RecSink sink;
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
    RecSink sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::CancelOrder x{};
    x.type = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum = 1000u;
    x.quantity = 0u;
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
    RecSink sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::CancelOrder x{};
    x.type = static_cast<char>(ouch::InType::CancelOrder);
    x.userRefNum = 1000u;
    x.quantity = 30u;
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
    RecSink sink;
    Venue<RecSink> v(sink, "AAPL", 1, 1, 100000, 100);
    v.onEnterOrder(makeEnter(1000, 'B', 100, "AAPL", kPx52), 1'000);
    sink.clear();

    ouch::ReplaceOrder u{};
    u.type = static_cast<char>(ouch::InType::ReplaceOrder);
    u.origUserRefNum = 1000u;
    u.userRefNum = 1001u;
    u.quantity = 150u;
    u.price = kPx51;
    u.timeInForce = static_cast<char>(ouch::TimeInForce::Day);
    u.display = static_cast<char>(ouch::Display::Visible);
    u.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    u.clOrdId = std::string_view{"CID2"};
    u.appendageLength = 0;
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

}

int main() {
    test_enter_rests();
    test_cross_against_synthetic();
    test_full_cancel();
    test_partial_cancel();
    test_replace_noncrossing();
    return abt::test::summary("venue_test");
}
