#pragma once
//
// Ethernet/IPv4/UDP framing for the market-data feed: build-once template + per-packet patch.
//

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "abt/protocol/EthIpUdp.hpp"

namespace abt::net {

struct FrameHeader {
    EthHeader  eth;
    Ipv4Header ip;
    UdpHeader  udp;
};
static_assert(sizeof(FrameHeader) == kL2L3L4Overhead);
static_assert(alignof(FrameHeader) == 1);
static_assert(std::is_trivially_copyable_v<FrameHeader>);

struct Endpoints {
    MacAddr       srcMac{};
    MacAddr       dstMac{};
    std::uint32_t srcIp{};
    std::uint32_t dstIp{};
    std::uint16_t srcPort{};
    std::uint16_t dstPort{};
};

class UdpFramer {
public:
    explicit UdpFramer(const Endpoints& ep) noexcept;

    [[nodiscard]] std::span<const std::byte> header() const noexcept {
        return std::as_bytes(std::span{&m_hdr, 1});
    }

    void patch(std::byte* frame, std::size_t payloadLen) const noexcept;

private:
    FrameHeader m_hdr{};
};

}
