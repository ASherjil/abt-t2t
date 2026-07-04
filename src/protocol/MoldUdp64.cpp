//
// MoldUDP64 Packer method definitions.
//

#include "abt/protocol/MoldUdp64.hpp"

namespace abt::mold {

Packer::Packer(std::string_view session, std::uint64_t firstSeq) : m_seq(firstSeq) {
    m_session.store(session);
}

void Packer::reset(std::byte* buf, std::size_t cap) noexcept {
    m_buf = buf;
    m_cap = cap;
    m_len = kHeaderSize;
    m_count = 0;
    m_firstSeq = m_seq;
}

bool Packer::append(std::span<const std::byte> msg) noexcept {
    const std::size_t need = 2 + msg.size();
    if (m_len + need > m_cap || m_count >= kEndOfSession - 1) return false;
    putU16(m_buf + m_len, static_cast<std::uint16_t>(msg.size()));
    std::memcpy(m_buf + m_len + 2, msg.data(), msg.size());
    m_len += need;
    ++m_count;
    ++m_seq;
    return true;
}

std::span<const std::byte> Packer::finalize() noexcept {
    writeHeader(m_firstSeq, m_count);
    return {m_buf, m_len};
}

std::span<const std::byte> Packer::heartbeat(std::byte* buf) noexcept {
    m_buf = buf;
    m_len = kHeaderSize;
    writeHeader(m_seq, kHeartbeat);
    return {m_buf, kHeaderSize};
}

std::span<const std::byte> Packer::endOfSession(std::byte* buf) noexcept {
    m_buf = buf;
    m_len = kHeaderSize;
    writeHeader(m_seq, kEndOfSession);
    return {m_buf, kHeaderSize};
}

void Packer::writeHeader(std::uint64_t seq, std::uint16_t count) noexcept {
    std::memcpy(m_buf, m_session.chars.data(), kSessionLen);
    putU64(m_buf + 10, seq);
    putU16(m_buf + 18, count);
}

}
