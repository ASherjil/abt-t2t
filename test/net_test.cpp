//
// Unit tests for the Ethernet/IPv4/UDP framing layer (abt::net).
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "TestHarness.hpp"

#include "abt/protocol/Checksum.hpp"
#include "abt/protocol/EthIpUdp.hpp"
#include "abt/protocol/UdpFramer.hpp"

using namespace abt;

namespace {

constexpr std::array<std::byte, 20> kIpVector{
    std::byte{0x45}, std::byte{0x00}, std::byte{0x00}, std::byte{0x73},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00},
    std::byte{0x40}, std::byte{0x11}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xc0}, std::byte{0xa8}, std::byte{0x00}, std::byte{0x01},
    std::byte{0xc0}, std::byte{0xa8}, std::byte{0x00}, std::byte{0xc7}};
static_assert(net::computeChecksum(kIpVector) == 0xB861u);

int byteAt(std::span<const std::byte> s, std::size_t i) {
    return std::to_integer<int>(s[i]);
}

net::Endpoints makeEndpoints() {
    return net::Endpoints{
        .srcMac  = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .dstMac  = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff},
        .srcIp   = net::ipv4(10, 0, 0, 1),
        .dstIp   = net::ipv4(10, 0, 0, 2),
        .srcPort = 40000,
        .dstPort = 41000};
}

void testChecksum() {
    CHECK_EQ(net::computeChecksum(kIpVector), 0xB861u);
}

void testTemplate() {
    net::UdpFramer fr{makeEndpoints()};
    auto h = fr.header();

    CHECK_EQ(h.size(), net::kL2L3L4Overhead);
    CHECK_EQ(byteAt(h, 0), 0xaa);
    CHECK_EQ(byteAt(h, 6), 0x11);
    CHECK_EQ(byteAt(h, 12), 0x08);
    CHECK_EQ(byteAt(h, 13), 0x00);
    CHECK_EQ(byteAt(h, 14), 0x45);
    CHECK_EQ(byteAt(h, 20), 0x40);
    CHECK_EQ(byteAt(h, 22), 64);
    CHECK_EQ(byteAt(h, 23), 17);
    CHECK_EQ(byteAt(h, 26), 10);
    CHECK_EQ(byteAt(h, 29), 1);
    CHECK_EQ(byteAt(h, 30), 10);
    CHECK_EQ(byteAt(h, 33), 2);
    CHECK_EQ(byteAt(h, 34), 0x9C);
    CHECK_EQ(byteAt(h, 35), 0x40);
    CHECK_EQ(byteAt(h, 36), 0xA0);
    CHECK_EQ(byteAt(h, 37), 0x28);
}

void testPatch() {
    net::UdpFramer fr{makeEndpoints()};

    constexpr std::size_t payloadLen = 18;
    std::array<std::byte, net::kL2L3L4Overhead + payloadLen> frame{};
    std::memcpy(frame.data(), fr.header().data(), net::kL2L3L4Overhead);
    fr.patch(frame.data(), payloadLen);

    CHECK_EQ(byteAt(frame, 16), 0x00);
    CHECK_EQ(byteAt(frame, 17), 0x2E);
    CHECK_EQ(byteAt(frame, 38), 0x00);
    CHECK_EQ(byteAt(frame, 39), 0x1A);
    CHECK_EQ(net::computeChecksum({frame.data() + net::kEthHeaderSize, net::kIpv4HeaderSize}), 0u);
    CHECK_EQ(byteAt(frame, 40), 0x00);
    CHECK_EQ(byteAt(frame, 41), 0x00);
}

}

int main() {
    testChecksum();
    testTemplate();
    testPatch();
    return abt::test::summary("net");
}
