//
// Drives ExchangeSession over real kernel sockets (socketpairs) end to end.
//

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

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

void setRecvTimeout(int fd, int ms) {
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

std::vector<std::byte> recvSome(int fd) {
    std::array<std::byte, 4096> buf{};
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) return {};
    return {buf.begin(), buf.begin() + n};
}

std::vector<soup::Packet> soupPackets(const std::vector<std::byte>& buf) {
    std::vector<soup::Packet> out;
    std::size_t off = 0;
    soup::Packet p{};
    while (true) {
        const std::size_t c = soup::parse({buf.data() + off, buf.size() - off}, p);
        if (c == 0) break;
        out.push_back(p);
        off += c;
    }
    return out;
}

std::vector<std::byte> recvUntilPackets(int fd, std::size_t want) {
    std::vector<std::byte> buf;
    for (int i = 0; i < 200 && soupPackets(buf).size() < want; ++i) {
        const auto chunk = recvSome(fd);
        if (chunk.empty()) break;
        buf.insert(buf.end(), chunk.begin(), chunk.end());
    }
    return buf;
}

void test_socket_roundtrip() {
    int oe[2];
    int md[2];
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, oe), 0);
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_DGRAM, 0, md), 0);
    setRecvTimeout(oe[0], 2000);
    setRecvTimeout(oe[1], 2000);
    setRecvTimeout(md[1], 2000);

    sim::ExchangeSession<sim::IoMode::Socket> ex{};
    ex.attachSockets(oe[0], md[0]);

    soup::LoginRequest lr{};
    lr.username = std::string_view{"USER01"};
    std::array<std::byte, 256> feed{};
    {
        const auto pkt = soup::pack(feed.data(), soup::Type::LoginRequest, bytesOf(lr));
        CHECK(::send(oe[1], pkt.data(), pkt.size(), 0) > 0);
    }
    ex.onOrderEntryBytes(recvSome(oe[0]), 1'000);
    {
        const auto buf = recvUntilPackets(oe[1], 1);
        const auto pkts = soupPackets(buf);
        CHECK_EQ(pkts.size(), 1u);
        CHECK(pkts[0].type == soup::Type::LoginAccepted);
    }

    ex.injectSynthetic(lob::Side::Sell, 5200, 100, 1'500);
    {
        const auto dg = recvSome(md[1]);
        std::size_t count = 0;
        mold::forEachMessage({dg.data(), dg.size()}, [&](std::uint64_t, std::span<const std::byte> m) {
            ++count;
            itch::AddOrder a{};
            std::memcpy(&a, m.data(), sizeof a);
            CHECK(a.messageType == 'A');
            CHECK(a.side == 'S');
        });
        CHECK_EQ(count, 1u);
    }

    ouch::EnterOrder o{};
    o.type = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum = 1000u;
    o.side = static_cast<char>(ouch::Side::Buy);
    o.quantity = 100u;
    o.symbol = std::string_view{"AAPL"};
    o.price = 520000u;
    o.timeInForce = static_cast<char>(ouch::TimeInForce::Day);
    o.display = static_cast<char>(ouch::Display::Visible);
    o.capacity = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId = std::string_view{"CID1"};
    o.appendageLength = 0;
    {
        const auto pkt = soup::packUnsequencedData(feed.data(), bytesOf(o));
        CHECK(::send(oe[1], pkt.data(), pkt.size(), 0) > 0);
    }
    ex.onOrderEntryBytes(recvSome(oe[0]), 2'000);
    {
        const auto buf = recvUntilPackets(oe[1], 2);
        const auto pkts = soupPackets(buf);
        CHECK_EQ(pkts.size(), 2u);
        CHECK(static_cast<char>(pkts[0].payload[0]) == static_cast<char>(ouch::OutType::Accepted));
        CHECK(static_cast<char>(pkts[1].payload[0]) == static_cast<char>(ouch::OutType::Executed));
        ouch::Executed e{};
        std::memcpy(&e, pkts[1].payload.data(), sizeof e);
        CHECK_EQ(e.quantity.value(), 100u);
        CHECK_EQ(e.price.value(), 520000u);
    }
    {
        const auto dg = recvSome(md[1]);
        std::size_t count = 0;
        mold::forEachMessage({dg.data(), dg.size()}, [&](std::uint64_t, std::span<const std::byte> m) {
            ++count;
            itch::OrderExecuted e{};
            std::memcpy(&e, m.data(), sizeof e);
            CHECK(e.messageType == 'E');
            CHECK_EQ(e.executedShares.value(), 100u);
        });
        CHECK_EQ(count, 1u);
    }
    CHECK_EQ(ex.bestAsk(), lob::kNoPrice);

    ::close(oe[1]); ::close(md[1]);
}

}

int main() {
    test_socket_roundtrip();
    return abt::test::summary("socketio_test");
}
