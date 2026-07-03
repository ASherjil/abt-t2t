#pragma once
//
// SoupBinTcp.hpp -- SoupBinTCP, Nasdaq's point-to-point TCP session protocol for
// guaranteed, sequenced delivery (it carries the OUCH order-entry stream).
//
// Every packet: [ PacketLength : u16 ][ PacketType : char ][ Payload : var ]
// where PacketLength counts Type + Payload (i.e. total on-wire = 2 + PacketLength).
//
// Roles in this simulator: the exchange is the SERVER, the DUT is the CLIENT.
//   client -> server : 'L' LoginRequest  'U' UnsequencedData(OUCH in)  'R' ClientHeartbeat  'O' LogoutRequest
//   server -> client : 'A' LoginAccepted 'J' LoginRejected  'S' SequencedData(OUCH out)
//                      'H' ServerHeartbeat  'Z' EndOfSession
//
// Sequenced Data packets carry no explicit number; the sequence starts at the value in
// Login Accepted and increments by one per 'S' packet. The sequence-number fields in the
// login handshake are 20-byte right-justified ASCII decimals (a SoupBin quirk).
//
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "abt/protocol/Endian.hpp"

namespace abt::soup {

inline constexpr std::size_t kHeaderSize = 3;   // u16 length + 1 type byte

enum class Type : char {
    // client -> server
    LoginRequest    = 'L',
    UnsequencedData = 'U',
    ClientHeartbeat = 'R',
    LogoutRequest   = 'O',
    // server -> client
    LoginAccepted   = 'A',
    LoginRejected   = 'J',
    SequencedData   = 'S',
    ServerHeartbeat = 'H',
    EndOfSession    = 'Z',
    Debug           = '+',
};

enum class RejectReason : char { NotAuthorized = 'A', SessionUnavailable = 'S' };

// 'L' Login Request payload (46 bytes).
struct LoginRequest {
    wire::Alpha<6>  username;
    wire::Alpha<10> password;
    wire::Alpha<10> requestedSession;
    wire::Alpha<20> requestedSequenceNumber;
};
static_assert(sizeof(LoginRequest) == 46);

// 'A' Login Accepted payload (30 bytes).
struct LoginAccepted {
    wire::Alpha<10> session;
    wire::Alpha<20> sequenceNumber;   // next Sequenced Data seq the server will send
};
static_assert(sizeof(LoginAccepted) == 30);

// 'J' Login Rejected payload (1 byte).
struct LoginRejected {
    char rejectReasonCode;   // RejectReason
};
static_assert(sizeof(LoginRejected) == 1);

// --- helpers ------------------------------------------------------------------------
inline void putU16(std::byte* p, std::uint16_t v) noexcept {
    wire::u16be t; t = v; std::memcpy(p, t.bytes.data(), 2);
}
inline std::uint16_t getU16(const std::byte* p) noexcept {
    wire::u16be t; std::memcpy(t.bytes.data(), p, 2); return t.value();
}
template <class T>
[[nodiscard]] std::span<const std::byte> asBytes(const T& t) noexcept {
    return {reinterpret_cast<const std::byte*>(&t), sizeof t};
}

// Format a sequence number as a 20-byte right-justified, space-padded ASCII decimal.
inline void formatSeq(wire::Alpha<20>& f, std::uint64_t v) noexcept {
    char tmp[20];
    int i = 20;
    do { tmp[--i] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v && i > 0);
    std::size_t digits = static_cast<std::size_t>(20 - i);
    std::size_t pad = 20 - digits;
    for (std::size_t k = 0; k < pad; ++k) f.chars[k] = ' ';
    std::memcpy(f.chars.data() + pad, tmp + i, digits);
}
inline std::uint64_t parseSeq(const wire::Alpha<20>& f) noexcept {
    std::uint64_t v = 0;
    for (char c : f.chars) {
        if (c >= '0' && c <= '9') v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return v;
}

// --- packet builder -----------------------------------------------------------------
// Write a packet of `type` wrapping `payload` into `buf`; returns the full on-wire span.
inline std::span<const std::byte> pack(std::byte* buf, Type type,
                                       std::span<const std::byte> payload) noexcept {
    const std::uint16_t plen = static_cast<std::uint16_t>(1 + payload.size());
    putU16(buf, plen);
    buf[2] = static_cast<std::byte>(static_cast<unsigned char>(type));
    if (!payload.empty()) std::memcpy(buf + kHeaderSize, payload.data(), payload.size());
    return {buf, static_cast<std::size_t>(2 + plen)};
}

inline std::span<const std::byte> packSequencedData(std::byte* buf,
                                                    std::span<const std::byte> ouch) noexcept {
    return pack(buf, Type::SequencedData, ouch);
}
inline std::span<const std::byte> packUnsequencedData(std::byte* buf,
                                                      std::span<const std::byte> ouch) noexcept {
    return pack(buf, Type::UnsequencedData, ouch);
}
inline std::span<const std::byte> packServerHeartbeat(std::byte* buf) noexcept {
    return pack(buf, Type::ServerHeartbeat, {});
}
inline std::span<const std::byte> packClientHeartbeat(std::byte* buf) noexcept {
    return pack(buf, Type::ClientHeartbeat, {});
}
inline std::span<const std::byte> packEndOfSession(std::byte* buf) noexcept {
    return pack(buf, Type::EndOfSession, {});
}
inline std::span<const std::byte> packLoginAccepted(std::byte* buf, std::string_view session,
                                                    std::uint64_t nextSeq) noexcept {
    LoginAccepted a{};
    a.session.store(session);
    formatSeq(a.sequenceNumber, nextSeq);
    return pack(buf, Type::LoginAccepted, asBytes(a));
}
inline std::span<const std::byte> packLoginRejected(std::byte* buf, RejectReason r) noexcept {
    LoginRejected j{static_cast<char>(r)};
    return pack(buf, Type::LoginRejected, asBytes(j));
}

// --- stream reader ------------------------------------------------------------------
struct Packet {
    Type                        type;
    std::span<const std::byte>  payload;
};

// Parse one packet from the front of a (possibly partial) TCP byte stream. Returns the
// number of bytes consumed, or 0 if a complete packet is not yet available.
[[nodiscard]] inline std::size_t parse(std::span<const std::byte> data, Packet& out) noexcept {
    if (data.size() < kHeaderSize) return 0;
    const std::uint16_t plen = getU16(data.data());
    if (plen == 0) return 0;                       // must include at least the type byte
    const std::size_t total = 2 + plen;
    if (data.size() < total) return 0;             // packet not fully arrived yet
    out.type = static_cast<Type>(static_cast<char>(data[2]));
    out.payload = data.subspan(kHeaderSize, plen - 1);
    return total;
}

}  // namespace abt::soup
