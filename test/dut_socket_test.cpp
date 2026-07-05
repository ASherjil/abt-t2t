//
// End-to-end integration over real kernel sockets: a live ExchangeSession (Socket mode) wired to
// a live DutSession (Socket mode) through socketpairs. The sim publishes market data, the DUT
// builds the book, its strategy crosses the spread, the order flows back over SoupBinTCP, and the
// sim matches it. This is the config-1 (Onload) datapath, exercised with no special hardware.
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include "TestHarness.hpp"

#include "abt/dut/DutSession.hpp"
#include "abt/dut/TakeStrategy.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/sim/EngineConfig.hpp"
#include "abt/sim/ExchangeSession.hpp"

using namespace abt;

namespace {

void setRecvTimeout(int fd, int ms) {
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

std::vector<std::byte> recvSome(int fd) {
    std::array<std::byte, 8192> buf{};
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
        return {};
    }
    return {buf.begin(), buf.begin() + n};
}

std::vector<soup::Packet> soupPackets(const std::vector<std::byte>& buf) {
    std::vector<soup::Packet> out;
    std::size_t off = 0;
    soup::Packet p{};
    while (true) {
        const std::size_t c = soup::parse({buf.data() + off, buf.size() - off}, p);
        if (c == 0) {
            break;
        }
        out.push_back(p);
        off += c;
    }
    return out;
}

// Accumulate from a stream fd until at least `want` SoupBinTCP packets have arrived.
std::vector<std::byte> recvUntilPackets(int fd, std::size_t want) {
    std::vector<std::byte> buf;
    for (int i = 0; i < 200 && soupPackets(buf).size() < want; ++i) {
        const auto chunk = recvSome(fd);
        if (chunk.empty()) {
            break;
        }
        buf.insert(buf.end(), chunk.begin(), chunk.end());
    }
    return buf;
}

void test_socket_integration() {
    int oe[2];
    int md[2];
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, oe), 0);
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_DGRAM, 0, md), 0);
    for (int fd : {oe[0], oe[1], md[0], md[1]}) {
        setRecvTimeout(fd, 2000);
    }

    // Sim: defaults (AAPL, minTick 1, wirePerTick 100 -> tick 52 == wire price 5200).
    ExchangeConfig simCfg{};
    ExchangeSession<IoMode::Socket> sim{simCfg};
    sim.attachSockets(oe[0], md[0]);

    // DUT: wire-price book stepped by 100, crosses once the offer reaches 5200.
    dut::DutConfig dutCfg{};
    dutCfg.minPrice = 0;
    dutCfg.maxPrice = 100000;
    dutCfg.tickWire = 100;
    dutCfg.symbol = "AAPL";
    dutCfg.firstUserRef = 1;
    dut::TakeStrategy strat(5200, 5u);
    dut::DutSession<dut::IoMode::Socket, dut::TakeStrategy> dutSess(dutCfg, strat);
    dutSess.attachSockets(oe[1], md[1]);

    // 1) DUT logs in; sim accepts; DUT ingests the LoginAccepted.
    dutSess.login(simCfg.session, "DUT001");
    sim.onOrderEntryBytes(recvSome(oe[0]), 1'000);
    dutSess.onOrderEntry(recvUntilPackets(oe[1], 1));
    CHECK(dutSess.sessionEstablished());

    // 2) Sim publishes a resting offer of 5 @ tick 52; the datagram reaches the DUT.
    sim.injectSynthetic(Side::Sell, 52, 5, 1'500);
    const auto feed = recvSome(md[1]);
    CHECK(!feed.empty());

    // 3) DUT ingests the feed -> book -> strategy crosses -> order goes out over SoupBinTCP.
    dutSess.onMarketData(feed, 2'000);
    CHECK_EQ(dutSess.ordersSent(), 1u);
    CHECK_EQ(dutSess.book().bestAsk(), 5200);

    // 4) Sim receives the DUT's marketable buy and matches it; the resting offer is consumed.
    sim.onOrderEntryBytes(recvSome(oe[0]), 2'500);
    CHECK_EQ(sim.bestAsk(), kNoPrice);

    // 5) The sim's OUCH acks (Accepted + Executed) come back to the DUT. Keep the byte buffer in a
    // named local — the parsed packets' payload spans point into it and must not outlive it.
    {
        const auto ackBytes = recvUntilPackets(oe[1], 2);
        const auto pkts = soupPackets(ackBytes);
        CHECK_EQ(pkts.size(), 2u);
        CHECK(static_cast<char>(pkts[0].payload[0]) == static_cast<char>(ouch::OutType::Accepted));
        CHECK(static_cast<char>(pkts[1].payload[0]) == static_cast<char>(ouch::OutType::Executed));
        ouch::Executed e{};
        std::memcpy(&e, pkts[1].payload.data(), sizeof e);
        CHECK_EQ(e.quantity.value(), 5u);
        CHECK_EQ(e.price.value(), 5200u);
    }
}

}

int main() {
    test_socket_integration();
    return abt::test::summary("dut_socket");
}
