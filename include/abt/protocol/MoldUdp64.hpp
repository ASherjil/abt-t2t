#pragma once
//
// MoldUdp64.hpp -- MoldUDP64, Nasdaq's UDP transport for sequenced message streams
// (it carries the ITCH market-data feed).
//
// Downstream packet layout:
//   [ Session : 10 ][ SequenceNumber : u64 ][ MessageCount : u16 ]  (20-byte header)
//   then MessageCount blocks, each:  [ MessageLength : u16 ][ MessageData : var ]
//
// SequenceNumber is the sequence of the FIRST message in the packet; message i carries
// SequenceNumber + i. MessageCount == 0 is a heartbeat (carries the next expected seq);
// MessageCount == 0xFFFF signals end of session. All integers are big-endian.
//
// This header provides a zero-copy Packer (build a datagram straight into a TX buffer)
// and reader helpers (iterate messages out of a received datagram).
//
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "abt/protocol/Endian.hpp"

namespace abt::mold {

inline constexpr std::size_t   kHeaderSize   = 20;
inline constexpr std::size_t   kSessionLen   = 10;
inline constexpr std::uint16_t kHeartbeat    = 0x0000;
inline constexpr std::uint16_t kEndOfSession = 0xFFFF;

// Overlay of the fixed header (for readers that want a typed view).
struct Header {
    wire::Alpha<kSessionLen> session;
    wire::u64be              sequenceNumber;
    wire::u16be              messageCount;
};
static_assert(sizeof(Header) == kHeaderSize);
static_assert(alignof(Header) == 1);

// --- little unaligned big-endian scalar helpers -------------------------------------
inline void putU16(std::byte* p, std::uint16_t v) noexcept {
    wire::u16be t; t = v; std::memcpy(p, t.bytes.data(), 2);
}
inline void putU64(std::byte* p, std::uint64_t v) noexcept {
    wire::u64be t; t = v; std::memcpy(p, t.bytes.data(), 8);
}
inline std::uint16_t getU16(const std::byte* p) noexcept {
    wire::u16be t; std::memcpy(t.bytes.data(), p, 2); return t.value();
}
inline std::uint64_t getU64(const std::byte* p) noexcept {
    wire::u64be t; std::memcpy(t.bytes.data(), p, 8); return t.value();
}

// Builds one MoldUDP64 datagram in place. Reused across packets; the running sequence
// number persists so consecutive packets are correctly numbered.
class Packer {
public:
    explicit Packer(std::string_view session, std::uint64_t firstSeq = 1) : m_seq(firstSeq) {
        m_session.store(session);
    }

    // Begin a new datagram in [buf, buf+cap). Records the sequence of its first message.
    void reset(std::byte* buf, std::size_t cap) noexcept {
        m_buf = buf;
        m_cap = cap;
        m_len = kHeaderSize;
        m_count = 0;
        m_firstSeq = m_seq;
    }

    // Append one message body. Returns false (without modifying the packet) if it would
    // overflow the buffer or the per-packet message limit -- caller should finalize+flush
    // and start a new packet.
    [[nodiscard]] bool append(std::span<const std::byte> msg) noexcept {
        const std::size_t need = 2 + msg.size();
        if (m_len + need > m_cap || m_count >= kEndOfSession - 1) return false;
        putU16(m_buf + m_len, static_cast<std::uint16_t>(msg.size()));
        std::memcpy(m_buf + m_len + 2, msg.data(), msg.size());
        m_len += need;
        ++m_count;
        ++m_seq;
        return true;
    }

    // Stamp the header and return the finished datagram.
    [[nodiscard]] std::span<const std::byte> finalize() noexcept {
        writeHeader(m_firstSeq, m_count);
        return {m_buf, m_len};
    }

    // A zero-message heartbeat carrying the next expected sequence number.
    [[nodiscard]] std::span<const std::byte> heartbeat(std::byte* buf) noexcept {
        m_buf = buf; m_len = kHeaderSize;
        writeHeader(m_seq, kHeartbeat);
        return {m_buf, kHeaderSize};
    }
    // End-of-session marker.
    [[nodiscard]] std::span<const std::byte> endOfSession(std::byte* buf) noexcept {
        m_buf = buf; m_len = kHeaderSize;
        writeHeader(m_seq, kEndOfSession);
        return {m_buf, kHeaderSize};
    }

    [[nodiscard]] std::uint64_t nextSequence() const noexcept { return m_seq; }
    [[nodiscard]] std::uint16_t count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t   size() const noexcept { return m_len; }

private:
    void writeHeader(std::uint64_t seq, std::uint16_t count) noexcept {
        std::memcpy(m_buf, m_session.chars.data(), kSessionLen);
        putU64(m_buf + 10, seq);
        putU16(m_buf + 18, count);
    }

    wire::Alpha<kSessionLen> m_session{};
    std::uint64_t m_seq;
    std::uint64_t m_firstSeq = 0;
    std::byte*    m_buf = nullptr;
    std::size_t   m_cap = 0;
    std::size_t   m_len = 0;
    std::uint16_t m_count = 0;
};

// --- reader helpers -----------------------------------------------------------------
[[nodiscard]] inline std::string_view sessionOf(std::span<const std::byte> pkt) noexcept {
    const auto* p = reinterpret_cast<const char*>(pkt.data());
    std::size_t n = kSessionLen;
    while (n > 0 && p[n - 1] == ' ') --n;
    return {p, n};
}
[[nodiscard]] inline std::uint64_t sequenceOf(std::span<const std::byte> pkt) noexcept {
    return getU64(pkt.data() + 10);
}
[[nodiscard]] inline std::uint16_t countOf(std::span<const std::byte> pkt) noexcept {
    return getU16(pkt.data() + 18);
}

// Invoke fn(sequenceNumber, messageBody) for each message in a datagram. Returns the
// number of messages delivered; stops cleanly on a truncated/malformed packet.
template <class Fn>
std::size_t forEachMessage(std::span<const std::byte> pkt, Fn&& fn) {
    if (pkt.size() < kHeaderSize) return 0;
    const std::uint64_t seq = sequenceOf(pkt);
    const std::uint16_t count = countOf(pkt);
    if (count == kHeartbeat || count == kEndOfSession) return 0;

    std::size_t off = kHeaderSize;
    std::size_t n = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (off + 2 > pkt.size()) break;
        const std::uint16_t mlen = getU16(pkt.data() + off);
        off += 2;
        if (off + mlen > pkt.size()) break;
        fn(seq + i, pkt.subspan(off, mlen));
        off += mlen;
        ++n;
    }
    return n;
}

}  // namespace abt::mold
