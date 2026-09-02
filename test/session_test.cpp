//
// End-to-end: SoupBinTCP in -> matching -> ITCH (MoldUDP64) + OUCH out, over loopback.
//

#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"
#include "t2t/sim/ExchangeSession.hpp"

using namespace abt;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& t) {
    return {reinterpret_cast<const std::byte*>(&t), sizeof t};
}

std::vector<std::vector<std::byte>> moldMessages(const std::vector<std::byte>& datagram) {
    std::vector<std::vector<std::byte>> out;
    mold::forEachMessage({datagram.data(), datagram.size()},
                         [&](std::uint64_t, std::span<const std::byte> m) {
                             out.emplace_back(m.begin(), m.end());
                         });
    return out;
}

soup::Packet soupOf(const std::vector<std::byte>& pkt) {
    soup::Packet p{};
    (void)soup::parse({pkt.data(), pkt.size()}, p);
    return p;
}

void test_end_to_end() {
    ExchangeSession<IoMode::Loopback> ex{};

    std::array<std::byte, 256> feed{};

    soup::LoginRequest lr{};
    lr.username         = std::string_view{"USER01"};
    lr.requestedSession = std::string_view{"SIM0000001"};
    {
        const auto pkt = soup::pack(feed.data(), soup::Type::LoginRequest, bytesOf(lr));
        ex.onOrderEntryBytes(pkt, 1'000);
    }
    CHECK_EQ(ex.capturedOrderEntry().size(), 1u);
    CHECK(soupOf(ex.capturedOrderEntry()[0]).type == soup::Type::LoginAccepted);
    ex.clearCaptured();

    const OrderId synthRef = ex.injectSynthetic(Side::Sell, 5200, 100, 1'500);
    CHECK_EQ(synthRef, 1u);
    CHECK_EQ(ex.capturedOrderEntry().size(), 0u);
    CHECK_EQ(ex.capturedMarketData().size(), 1u);
    {
        const auto msgs = moldMessages(ex.capturedMarketData()[0]);
        CHECK_EQ(msgs.size(), 1u);
        CHECK_EQ(msgs[0].size(), sizeof(itch::AddOrder));
        itch::AddOrder a{};
        std::memcpy(&a, msgs[0].data(), sizeof a);
        CHECK(a.messageType == itch::MessageType::AddOrder);
        CHECK(a.side == itch::Side::Sell);
        CHECK_EQ(a.shares.value(), 100u);
        CHECK_EQ(a.price.value(), 520000u);
        CHECK_EQ(a.orderRef.value(), 1u);
    }
    ex.clearCaptured();

    ouch::EnterOrder o{};
    o.type               = ouch::InType::EnterOrder;
    o.userRefNum         = 1000u;
    o.side               = ouch::Side::Buy;
    o.quantity           = 100u;
    o.symbol             = std::string_view{"AAPL"};
    o.price              = 520000u;
    o.timeInForce        = ouch::TimeInForce::Day;
    o.display            = ouch::Display::Visible;
    o.capacity           = ouch::Capacity::Agency;
    o.imSweepEligibility = ouch::ImSweep::NotEligible;
    o.crossType          = ouch::CrossType::Continuous;
    o.clOrdId            = std::string_view{"CID1"};
    o.appendageLength    = 0;
    {
        const auto pkt = soup::packUnsequencedData(feed.data(), bytesOf(o));
        ex.onOrderEntryBytes(pkt, 2'000);
    }

    CHECK_EQ(ex.capturedOrderEntry().size(), 2u);
    const auto acc = soupOf(ex.capturedOrderEntry()[0]);
    const auto exe = soupOf(ex.capturedOrderEntry()[1]);
    CHECK(acc.type == soup::Type::SequencedData);
    CHECK(exe.type == soup::Type::SequencedData);
    CHECK(static_cast<ouch::OutType>(acc.payload[0]) == ouch::OutType::Accepted);
    CHECK(static_cast<ouch::OutType>(exe.payload[0]) == ouch::OutType::Executed);
    {
        ouch::Executed e{};
        std::memcpy(&e, exe.payload.data(), sizeof e);
        CHECK_EQ(e.userRefNum.value(), 1000u);
        CHECK_EQ(e.quantity.value(), 100u);
        CHECK_EQ(e.price.value(), 520000u);
        CHECK_EQ(e.matchNumber.value(), 1u);
    }

    CHECK_EQ(ex.capturedMarketData().size(), 1u);
    {
        const auto msgs = moldMessages(ex.capturedMarketData()[0]);
        CHECK_EQ(msgs.size(), 1u);
        itch::OrderExecuted e{};
        std::memcpy(&e, msgs[0].data(), sizeof e);
        CHECK(e.messageType == itch::MessageType::OrderExecuted);
        CHECK_EQ(e.orderRef.value(), 1u);
        CHECK_EQ(e.executedShares.value(), 100u);
        CHECK_EQ(e.matchNumber.value(), 1u);
    }
    CHECK_EQ(ex.bestAsk(), kNoPrice);
}

void test_partial_stream() {
    ExchangeSession<IoMode::Loopback> ex{};

    std::array<std::byte, 256> feed{};
    soup::LoginRequest         lr{};
    lr.username    = std::string_view{"USER01"};
    const auto pkt = soup::pack(feed.data(), soup::Type::LoginRequest, bytesOf(lr));

    ex.onOrderEntryBytes(pkt.subspan(0, 5), 1'000);
    CHECK_EQ(ex.capturedOrderEntry().size(), 0u);
    ex.onOrderEntryBytes(pkt.subspan(5), 1'000);
    CHECK_EQ(ex.capturedOrderEntry().size(), 1u);
    CHECK(soupOf(ex.capturedOrderEntry()[0]).type == soup::Type::LoginAccepted);
}

}   // namespace

ouch::EnterOrder enterFor(std::string_view symbol, std::uint32_t userRef, ouch::Side side, Quantity qty,
                          std::uint64_t price) {
    ouch::EnterOrder o{};
    o.type               = ouch::InType::EnterOrder;
    o.userRefNum         = userRef;
    o.side               = side;
    o.quantity           = qty;
    o.symbol             = symbol;
    o.price              = price;
    o.timeInForce        = ouch::TimeInForce::Day;
    o.display            = ouch::Display::Visible;
    o.capacity           = ouch::Capacity::Principal;
    o.imSweepEligibility = ouch::ImSweep::NotEligible;
    o.crossType          = ouch::CrossType::Continuous;
    o.clOrdId            = std::string_view{};
    o.appendageLength    = 0;
    return o;
}

std::vector<std::vector<std::byte>> allMoldMessages(const std::vector<std::vector<std::byte>>& pkts) {
    std::vector<std::vector<std::byte>> out;
    for (const auto& p : pkts) {
        for (auto& m : moldMessages(p)) {
            out.push_back(std::move(m));
        }
    }
    return out;
}

void test_two_venues_route_by_symbol_and_user_ref() {
    ExchangeConfig cfg{};
    cfg.symbols = {"AAPL", "MSFT"};
    cfg.locates = {13, 7};
    ExchangeSession<IoMode::Loopback> ex{cfg};
    CHECK_EQ(ex.venueCount(), 2u);
    std::array<std::byte, 256> feed{};

    ex.publishDirectory(500);
    {
        const auto msgs = allMoldMessages(ex.capturedMarketData());
        CHECK_EQ(msgs.size(), 2u);
        itch::StockDirectory r{};
        std::memcpy(&r, msgs[1].data(), sizeof r);
        CHECK(r.messageType == itch::MessageType::StockDirectory);
        CHECK_EQ(r.stockLocate.value(), 7u);
        CHECK(r.stock.view() == "MSFT");
    }
    ex.clearCaptured();

    const auto msft = enterFor("MSFT", 41u, ouch::Side::Sell, 100u, 1'700'000u);
    ex.onOrderEntryBytes(soup::packUnsequencedData(feed.data(), bytesOf(msft)), 1'000);
    CHECK_EQ(ex.capturedOrderEntry().size(), 1u);
    CHECK(static_cast<ouch::OutType>(soupOf(ex.capturedOrderEntry()[0]).payload[0]) ==
          ouch::OutType::Accepted);
    {
        const auto msgs = allMoldMessages(ex.capturedMarketData());
        CHECK_EQ(msgs.size(), 1u);
        itch::AddOrder a{};
        std::memcpy(&a, msgs[0].data(), sizeof a);
        CHECK_EQ(a.stockLocate.value(), 7u);
        CHECK(a.stock.view() == "MSFT");
        CHECK_EQ(a.price.value(), 1'700'000u);
    }
    CHECK_EQ(ex.venue(1).bestAsk(), 17000);
    CHECK_EQ(ex.venue(0).bestAsk(), kNoPrice);
    CHECK_EQ(ex.liveOrders(), 1u);
    ex.clearCaptured();

    const auto nope = enterFor("ZZZZ", 42u, ouch::Side::Buy, 10u, 100u);
    ex.onOrderEntryBytes(soup::packUnsequencedData(feed.data(), bytesOf(nope)), 1'100);
    CHECK_EQ(ex.capturedOrderEntry().size(), 1u);
    CHECK(static_cast<ouch::OutType>(soupOf(ex.capturedOrderEntry()[0]).payload[0]) ==
          ouch::OutType::Rejected);
    ex.clearCaptured();

    ouch::CancelOrder x{};
    x.type            = ouch::InType::CancelOrder;
    x.userRefNum      = 41u;
    x.quantity        = 0;
    x.appendageLength = 0;
    ex.onOrderEntryBytes(soup::packUnsequencedData(feed.data(), bytesOf(x)), 1'200);
    CHECK_EQ(ex.capturedOrderEntry().size(), 1u);
    CHECK(static_cast<ouch::OutType>(soupOf(ex.capturedOrderEntry()[0]).payload[0]) ==
          ouch::OutType::Canceled);
    CHECK_EQ(ex.venue(1).bestAsk(), kNoPrice);
    CHECK_EQ(ex.liveOrders(), 0u);
    ex.clearCaptured();

    itch::StockDirectory r{};
    r.messageType = itch::MessageType::StockDirectory;
    r.stockLocate = 900;
    r.stock       = std::string_view{"MSFT"};
    ex.replayMessage(bytesOf(r), 2'000);
    CHECK_EQ(ex.venue(1).stockLocate(), 900u);
    itch::AddOrder real{};
    real.messageType = itch::MessageType::AddOrder;
    real.stockLocate = 900;
    real.orderRef    = 77u;
    real.side        = itch::Side::Buy;
    real.shares      = 50u;
    real.price       = 1'690'000u;
    ex.replayMessage(bytesOf(real), 2'100);
    real.stockLocate = 7;
    real.orderRef    = 78u;
    ex.replayMessage(bytesOf(real), 2'200);
    ex.flushMarketData();
    CHECK_EQ(ex.mirrorStats().adds, 1u);
    CHECK_EQ(ex.venue(1).bestBid(), 16900);
    CHECK_EQ(ex.stats().forwarded, 3u);
}

int main() {
    test_two_venues_route_by_symbol_and_user_ref();
    test_end_to_end();
    test_partial_stream();
    return abt::test::summary("session_test");
}
