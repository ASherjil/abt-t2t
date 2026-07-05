#pragma once
//
// Ethernet II / IPv4 / UDP header overlays for hand-built L2-L4 framing (ef_vi / DPDK datapaths).
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "abt/protocol/Endian.hpp"

namespace abt::net {

inline constexpr std::size_t kEthHeaderSize  = 14;
inline constexpr std::size_t kIpv4HeaderSize = 20;
inline constexpr std::size_t kUdpHeaderSize  = 8;
inline constexpr std::size_t kL2L3L4Overhead = kEthHeaderSize + kIpv4HeaderSize + kUdpHeaderSize;

inline constexpr std::uint16_t kEtherTypeIpv4    = 0x0800;
inline constexpr std::uint8_t  kIpv4VerIhl       = 0x45;
inline constexpr std::uint8_t  kIpProtocolUdp    = 17;
inline constexpr std::uint8_t  kDefaultTtl       = 64;
inline constexpr std::uint16_t kFlagDontFragment = 0x4000;

using MacAddr = std::array<std::uint8_t, 6>;

[[nodiscard]] constexpr std::uint32_t ipv4(std::uint8_t a, std::uint8_t b,
                                           std::uint8_t c, std::uint8_t d) noexcept {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16)
         | (static_cast<std::uint32_t>(c) << 8)  |  static_cast<std::uint32_t>(d);
}

struct EthHeader {
    MacAddr     dstMac;
    MacAddr     srcMac;
    wire::u16be etherType;
};
static_assert(sizeof(EthHeader) == kEthHeaderSize);
static_assert(alignof(EthHeader) == 1);
static_assert(std::is_trivially_copyable_v<EthHeader>);

struct Ipv4Header {
    std::uint8_t verIhl;
    std::uint8_t dscpEcn;
    wire::u16be  totalLen;
    wire::u16be  ident;
    wire::u16be  flagsFrag;
    std::uint8_t ttl;
    std::uint8_t protocol;
    wire::u16be  checksum;
    wire::u32be  srcIp;
    wire::u32be  dstIp;
};
static_assert(sizeof(Ipv4Header) == kIpv4HeaderSize);
static_assert(alignof(Ipv4Header) == 1);
static_assert(std::is_trivially_copyable_v<Ipv4Header>);

struct UdpHeader {
    wire::u16be srcPort;
    wire::u16be dstPort;
    wire::u16be length;
    wire::u16be checksum;
};
static_assert(sizeof(UdpHeader) == kUdpHeaderSize);
static_assert(alignof(UdpHeader) == 1);
static_assert(std::is_trivially_copyable_v<UdpHeader>);

}
