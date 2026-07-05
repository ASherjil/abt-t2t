//
// Unit test for the DPDK market-data TX path: ExchangeSession<IoMode::Dpdk, MockTx> end-to-end
// (venue -> MoldUDP64 packer -> Eth/IPv4/UDP framer -> transport) with a mock transport that
// mirrors ABTRDA3's prefillRing/acquire/commit contract. No DPDK/NIC needed.
//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "TestHarness.hpp"

#include "abt/protocol/Checksum.hpp"
#include "abt/protocol/EthIpUdp.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/sim/ExchangeSession.hpp"

using namespace abt;

namespace {

struct MockTx {
    std::vector<std::uint8_t>              tmpl;
    std::vector<std::uint8_t>              scratch;
    std::vector<std::vector<std::uint8_t>> frames;

    void prefillRing(std::span<const std::uint8_t> t) { tmpl.assign(t.begin(), t.end()); }
    std::uint8_t* acquire(std::uint32_t n) {
        scratch.assign(n, 0);
        std::memcpy(scratch.data(), tmpl.data(), std::min<std::size_t>(tmpl.size(), n));
        return scratch.data();
    }
    void commit() { frames.emplace_back(scratch); }
};

net::Endpoints makeEndpoints() {
    return net::Endpoints{
        .srcMac  = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .dstMac  = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff},
        .srcIp   = net::ipv4(10, 0, 0, 1),
        .dstIp   = net::ipv4(10, 0, 0, 2),
        .srcPort = 40000,
        .dstPort = 41000};
}

void testMarketDataFrame() {
    ExchangeSession<IoMode::Dpdk, MockTx> ex{};
    MockTx tx;
    ex.prepareDpdk(tx, makeEndpoints());

    CHECK_EQ(tx.tmpl.size(), net::kL2L3L4Overhead);

    ex.injectSynthetic(Side::Buy, 5000, 100, 1000);

    CHECK_EQ(tx.frames.size(), 1u);
    if (tx.frames.empty()) return;

    const auto& f = tx.frames.front();
    std::span<const std::byte> frame{reinterpret_cast<const std::byte*>(f.data()), f.size()};

    CHECK(frame.size() > net::kL2L3L4Overhead);
    CHECK_EQ(std::to_integer<int>(frame[12]), 0x08);
    CHECK_EQ(std::to_integer<int>(frame[13]), 0x00);
    CHECK_EQ(std::to_integer<int>(frame[14]), 0x45);
    CHECK_EQ(std::to_integer<int>(frame[0]), 0xaa);
    CHECK_EQ(std::to_integer<int>(frame[6]), 0x11);
    CHECK_EQ(std::to_integer<int>(frame[26]), 10);
    CHECK_EQ(std::to_integer<int>(frame[29]), 1);

    CHECK_EQ(mold::getU16(frame.data() + 16), frame.size() - net::kEthHeaderSize);
    CHECK_EQ(mold::getU16(frame.data() + 38),
             frame.size() - net::kEthHeaderSize - net::kIpv4HeaderSize);
    CHECK_EQ(net::computeChecksum({frame.data() + net::kEthHeaderSize, net::kIpv4HeaderSize}), 0u);

    auto payload = frame.subspan(net::kL2L3L4Overhead);
    CHECK_EQ(mold::countOf(payload), 1u);

    int msgs = 0;
    std::size_t msgSize = 0;
    char msgType = 0;
    mold::forEachMessage(payload, [&](std::uint64_t, std::span<const std::byte> m) {
        ++msgs;
        msgSize = m.size();
        if (!m.empty()) msgType = static_cast<char>(m[0]);
    });
    CHECK_EQ(msgs, 1);
    CHECK_EQ(msgSize, 36u);
    CHECK_EQ(msgType, 'A');
}

}

int main() {
    testMarketDataFrame();
    return abt::test::summary("dpdk");
}
