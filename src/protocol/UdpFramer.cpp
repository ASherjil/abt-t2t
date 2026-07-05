//
// Ethernet/IPv4/UDP framing: template construction and per-packet field patching.
//

#include "abt/protocol/UdpFramer.hpp"

#include "abt/protocol/Checksum.hpp"

namespace abt::net {

UdpFramer::UdpFramer(const Endpoints& ep) noexcept {
    m_hdr.eth.dstMac    = ep.dstMac;
    m_hdr.eth.srcMac    = ep.srcMac;
    m_hdr.eth.etherType = kEtherTypeIpv4;

    m_hdr.ip.verIhl    = kIpv4VerIhl;
    m_hdr.ip.dscpEcn   = 0;
    m_hdr.ip.totalLen  = 0;
    m_hdr.ip.ident     = 0;
    m_hdr.ip.flagsFrag = kFlagDontFragment;
    m_hdr.ip.ttl       = kDefaultTtl;
    m_hdr.ip.protocol  = kIpProtocolUdp;
    m_hdr.ip.checksum  = 0;
    m_hdr.ip.srcIp     = ep.srcIp;
    m_hdr.ip.dstIp     = ep.dstIp;

    m_hdr.udp.srcPort  = ep.srcPort;
    m_hdr.udp.dstPort  = ep.dstPort;
    m_hdr.udp.length   = 0;
    m_hdr.udp.checksum = 0;
}

void UdpFramer::patch(std::byte* frame, std::size_t payloadLen) const noexcept {
    auto* ip  = reinterpret_cast<Ipv4Header*>(frame + kEthHeaderSize);
    auto* udp = reinterpret_cast<UdpHeader*>(frame + kEthHeaderSize + kIpv4HeaderSize);

    ip->totalLen = static_cast<std::uint16_t>(kIpv4HeaderSize + kUdpHeaderSize + payloadLen);
    udp->length  = static_cast<std::uint16_t>(kUdpHeaderSize + payloadLen);

    ip->checksum = 0;
    ip->checksum = computeChecksum({frame + kEthHeaderSize, kIpv4HeaderSize});
}

}
