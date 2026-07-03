//
// session_test.cpp -- drives the whole exchange simulator in software over a loopback
// I/O boundary: SoupBinTCP in -> OUCH decode -> matching -> ITCH out (MoldUDP64) and
// OUCH out (SoupBinTCP). This exercises every layer end to end without a NIC.
//
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "abt/net/LoopbackIo.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "TestHarness.hpp"

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

// Unwrap a SoupBinTCP packet captured on the order-entry stream.
soup::Packet soupOf(const std::vector<std::byte>& pkt) {
    soup::Packet p{};
    soup::parse({pkt.data(), pkt.size()}, p);
    return p;
}

void test_end_to_end() {
    net::LoopbackIo io;
    sim::ExchangeSession<net::LoopbackIo> ex(io, {});   // default config: AAPL, SIM0000001

    std::array<std::byte, 256> feed{};

    // --- 1. Client logs in -> server replies LoginAccepted -------------------------
    soup::LoginRequest lr{};
    lr.username = std::string_view{"USER01"};
    lr.requestedSession = std::string_view{"SIM0000001"};
    {
        const auto pkt = soup::pack(feed.data(), soup::Type::LoginRequest, bytesOf(lr));
        ex.onOrderEntryBytes(pkt, 1'000);
    }
    CHECK_EQ(io.oe.size(), 1u);
    CHECK(soupOf(io.oe[0]).type == soup::Type::LoginAccepted);
    io.clear();

    // --- 2. A synthetic resting sell appears on the ITCH feed (no OUCH) ------------
    const lob::OrderId synthRef = ex.injectSynthetic(lob::Side::Sell, 5200, 100, 1'500);
    CHECK_EQ(synthRef, 1u);
    CHECK_EQ(io.oe.size(), 0u);
    CHECK_EQ(io.md.size(), 1u);
    {
        const auto msgs = moldMessages(io.md[0]);
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
    io.clear();

    // --- 3. Client sends a marketable OUCH buy -> executions on both streams --------
    ouch::EnterOrder o{};
    o.type = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum = 1000u;
    o.side = static_cast<char>(ouch::Side::Buy);
    o.quantity = 100u;
    o.symbol = std::string_view{"AAPL"};
    o.price = 520000u;               // $52.00 -> crosses the synthetic sell
    o.timeInForce = static_cast<char>(ouch::TimeInForce::Day);
    o.display = static_cast<char>(ouch::Display::Visible);
    o.capacity = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId = std::string_view{"CID1"};
    o.appendageLength = 0;
    {
        const auto pkt = soup::packUnsequencedData(feed.data(), bytesOf(o));
        ex.onOrderEntryBytes(pkt, 2'000);
    }

    // OUCH order entry: Accepted then Executed, each in its own SoupBin 'S' packet.
    CHECK_EQ(io.oe.size(), 2u);
    const auto acc = soupOf(io.oe[0]);
    const auto exe = soupOf(io.oe[1]);
    CHECK(acc.type == soup::Type::SequencedData);
    CHECK(exe.type == soup::Type::SequencedData);
    CHECK(static_cast<char>(acc.payload[0]) == static_cast<char>(ouch::OutType::Accepted));
    CHECK(static_cast<char>(exe.payload[0]) == static_cast<char>(ouch::OutType::Executed));
    {
        ouch::Executed e{};
        std::memcpy(&e, exe.payload.data(), sizeof e);
        CHECK_EQ(e.userRefNum.value(), 1000u);
        CHECK_EQ(e.quantity.value(), 100u);
        CHECK_EQ(e.price.value(), 520000u);
        CHECK_EQ(e.matchNumber.value(), 1u);
    }

    // ITCH market data: OrderExecuted for the resting synthetic order.
    CHECK_EQ(io.md.size(), 1u);
    {
        const auto msgs = moldMessages(io.md[0]);
        CHECK_EQ(msgs.size(), 1u);
        itch::OrderExecuted e{};
        std::memcpy(&e, msgs[0].data(), sizeof e);
        CHECK(e.messageType == 'E');
        CHECK_EQ(e.orderRef.value(), 1u);
        CHECK_EQ(e.executedShares.value(), 100u);
        CHECK_EQ(e.matchNumber.value(), 1u);
    }
    CHECK_EQ(ex.bestAsk(), lob::kNoPrice);   // synthetic fully consumed
}

// The SoupBinTCP stream reassembler must handle a packet split across two reads.
void test_partial_stream() {
    net::LoopbackIo io;
    sim::ExchangeSession<net::LoopbackIo> ex(io, {});

    std::array<std::byte, 256> feed{};
    soup::LoginRequest lr{};
    lr.username = std::string_view{"USER01"};
    const auto pkt = soup::pack(feed.data(), soup::Type::LoginRequest, bytesOf(lr));

    ex.onOrderEntryBytes(pkt.subspan(0, 5), 1'000);    // first fragment: no full packet yet
    CHECK_EQ(io.oe.size(), 0u);
    ex.onOrderEntryBytes(pkt.subspan(5), 1'000);       // rest arrives -> packet completes
    CHECK_EQ(io.oe.size(), 1u);
    CHECK(soupOf(io.oe[0]).type == soup::Type::LoginAccepted);
}

}  // namespace

int main() {
    test_end_to_end();
    test_partial_stream();
    return abt::test::summary("session_test");
}
