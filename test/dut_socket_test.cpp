#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "t2t/dut/DutSession.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"
#include "t2t/sim/EngineConfig.hpp"
#include "t2t/sim/ExchangeSession.hpp"

using namespace abt;

namespace {

struct TakeOnce {
    Price    trigger;
    Quantity qty;
    bool     armed = true;

    dut::QuoteTargets onBook(const dut::BookBuilder& book, const dut::Account&) noexcept {
        dut::QuoteTargets t{};
        const Price       ask = book.bestAsk();
        if (ask == kNoPrice || ask > trigger || !armed) {
            return t;
        }
        armed      = false;
        t.quoteBid = true;
        t.bidPrice = ask;
        t.bidQty   = qty;
        return t;
    }
};

void setRecvTimeout(int fd, int ms) {
    timeval tv{.tv_sec = ms / 1000, .tv_usec = static_cast<__suseconds_t>((ms % 1000) * 1000)};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

std::vector<std::byte> recvSome(int fd) {
    std::array<std::byte, 8192> buf{};
    const ssize_t               n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
        return {};
    }
    return {buf.begin(), buf.begin() + n};
}

std::size_t countSoupPackets(const std::vector<std::byte>& buf) {
    std::size_t  n   = 0;
    std::size_t  off = 0;
    soup::Packet p{};
    while (true) {
        const std::size_t c = soup::parse({buf.data() + off, buf.size() - off}, p);
        if (c == 0) {
            break;
        }
        ++n;
        off += c;
    }
    return n;
}

std::vector<std::byte> recvUntilPackets(int fd, std::size_t want) {
    std::vector<std::byte> buf;
    for (int i = 0; i < 200 && countSoupPackets(buf) < want; ++i) {
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
    for (const int fd : {oe[0], oe[1], md[0], md[1]}) {
        setRecvTimeout(fd, 2000);
    }

    const ExchangeConfig            simCfg{};
    ExchangeSession<IoMode::Socket> sim{simCfg};
    sim.attachSockets(oe[0], md[0]);

    dut::DutConfig dutCfg{};
    dutCfg.minPrice     = 0;
    dutCfg.maxPrice     = 100000;
    dutCfg.tickWire     = 100;
    dutCfg.symbol       = "AAPL";
    dutCfg.firstUserRef = 1;
    dut::DutSession<dut::IoMode::Socket, TakeOnce> dutSess(dutCfg, TakeOnce{.trigger = 5200, .qty = 5u});
    dutSess.attachSockets(oe[1], md[1]);

    dutSess.login(simCfg.session, "DUT001");
    sim.onOrderEntryBytes(recvSome(oe[0]), 1'000);
    dutSess.onOrderEntry(recvUntilPackets(oe[1], 1));
    CHECK(dutSess.sessionEstablished());

    sim.injectSynthetic(Side::Sell, 52, 5, 1'500);
    const auto feed = recvSome(md[1]);
    CHECK(!feed.empty());

    dutSess.onMarketData(feed, 2'000);
    CHECK_EQ(dutSess.ordersSent(), 1u);
    CHECK_EQ(dutSess.book().bestAsk(), 5200);
    CHECK(dutSess.oms().slot(Side::Buy).state == dut::QuoteState::PendingNew);

    sim.onOrderEntryBytes(recvSome(oe[0]), 2'500);
    CHECK_EQ(sim.bestAsk(), kNoPrice);

    dutSess.onOrderEntry(recvUntilPackets(oe[1], 2));
    CHECK_EQ(dutSess.oms().stats().accepts, 1u);
    CHECK_EQ(dutSess.oms().stats().fills, 1u);
    CHECK_EQ(dutSess.oms().account().position, 5);
    CHECK(dutSess.oms().slot(Side::Buy).state == dut::QuoteState::Idle);

    const auto execFeed = recvSome(md[1]);
    CHECK(!execFeed.empty());
    dutSess.onMarketData(execFeed, 3'000);
    CHECK_EQ(dutSess.book().bestAsk(), kNoPrice);
    CHECK_EQ(dutSess.feed().gaps(), 0u);
}

}   // namespace

int main() {
    test_socket_integration();
    return abt::test::summary("dut_socket");
}
