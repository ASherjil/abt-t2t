#pragma once
//
// MoldUDP64 market-data framing: sequenced-packet Packer and reader.
//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "t2t/protocol/Endian.hpp"

namespace abt::mold {

inline constexpr std::size_t   kHeaderSize     = 20;
inline constexpr std::size_t   kSessionLen     = 10;
inline constexpr std::size_t   kSequenceOffset = kSessionLen;
inline constexpr std::size_t   kCountOffset    = kSequenceOffset + sizeof(std::uint64_t);
inline constexpr std::size_t   kLengthPrefix   = sizeof(std::uint16_t);
inline constexpr std::uint16_t kHeartbeat      = 0x0000;
inline constexpr std::uint16_t kEndOfSession   = 0xFFFF;

struct Header {
    wire::Alpha<kSessionLen> session;
    wire::u64be              sequenceNumber;
    wire::u16be              messageCount;
};

static_assert(sizeof(Header) == kHeaderSize);
static_assert(alignof(Header) == 1);

inline void putU16(std::byte* p, std::uint16_t v) noexcept {
    wire::u16be t;
    t = v;
    std::memcpy(p, t.bytes.data(), 2);
}

inline void putU64(std::byte* p, std::uint64_t v) noexcept {
    wire::u64be t;
    t = v;
    std::memcpy(p, t.bytes.data(), 8);
}

inline std::uint16_t getU16(const std::byte* p) noexcept {
    wire::u16be t;
    std::memcpy(t.bytes.data(), p, 2);
    return t.value();
}

inline std::uint64_t getU64(const std::byte* p) noexcept {
    wire::u64be t;
    std::memcpy(t.bytes.data(), p, 8);
    return t.value();
}

class Packer {
public:
    explicit Packer(std::string_view session, std::uint64_t firstSeq = 1);

    void                                     reset(std::byte* buf, std::size_t cap) noexcept;
    [[nodiscard]] bool                       append(std::span<const std::byte> msg) noexcept;
    [[nodiscard]] std::span<const std::byte> finalize() noexcept;
    [[nodiscard]] std::span<const std::byte> heartbeat(std::byte* buf) noexcept;
    [[nodiscard]] std::span<const std::byte> endOfSession(std::byte* buf) noexcept;

    [[nodiscard]] std::uint64_t nextSequence() const noexcept {
        return m_seq;
    }

    [[nodiscard]] std::uint16_t count() const noexcept {
        return m_count;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_len;
    }

private:
    void writeHeader(std::uint64_t seq, std::uint16_t count) noexcept;

    wire::Alpha<kSessionLen> m_session{};
    std::uint64_t            m_seq;
    std::uint64_t            m_firstSeq = 0;
    std::byte*               m_buf      = nullptr;
    std::size_t              m_cap      = 0;
    std::size_t              m_len      = 0;
    std::uint16_t            m_count    = 0;
};

[[nodiscard]] inline std::string_view sessionOf(std::span<const std::byte> pkt) noexcept {
    const auto* p = reinterpret_cast<const char*>(pkt.data());
    std::size_t n = kSessionLen;
    while (n > 0 && p[n - 1] == ' ') {
        --n;
    }
    return {p, n};
}

[[nodiscard]] inline std::uint64_t sequenceOf(std::span<const std::byte> pkt) noexcept {
    return getU64(pkt.data() + kSequenceOffset);
}

[[nodiscard]] inline std::uint16_t countOf(std::span<const std::byte> pkt) noexcept {
    return getU16(pkt.data() + kCountOffset);
}

[[nodiscard]] inline bool nextMessage(const std::byte* base, std::size_t total, std::size_t& off,
                                      std::span<const std::byte>& msg) noexcept {
    if (off + kLengthPrefix > total) {
        return false;
    }
    const std::uint16_t mlen = getU16(base + off);
    if (off + kLengthPrefix + mlen > total) {
        return false;
    }
    msg = {base + off + kLengthPrefix, mlen};
    off += kLengthPrefix + mlen;
    return true;
}

template <class Fn>
std::size_t forEachMessage(std::span<const std::byte> pkt, Fn fn) {
    if (pkt.size() < kHeaderSize) {
        return 0;
    }
    const std::uint64_t seq   = sequenceOf(pkt);
    const std::uint16_t count = countOf(pkt);
    if (count == kHeartbeat || count == kEndOfSession) {
        return 0;
    }

    std::size_t                off = kHeaderSize;
    std::size_t                n   = 0;
    std::span<const std::byte> msg;
    for (std::uint16_t i = 0; i < count && nextMessage(pkt.data(), pkt.size(), off, msg); ++i) {
        fn(seq + i, msg);
        ++n;
    }
    return n;
}

}   // namespace abt::mold
