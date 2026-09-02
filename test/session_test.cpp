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
        CHECK(a.messageType == 'A');
        CHECK(a.side == 'S');
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
        CHECK(e.messageType == 'E');
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

int main() {
    test_end_to_end();
    test_partial_stream();
    return abt::test::summary("session_test");
}
