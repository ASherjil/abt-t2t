#pragma once
//
// SoupBinTCP order-entry session framing: login, sequenced data, heartbeats, stream reader.
//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "abt/protocol/Endian.hpp"

namespace abt::soup {

inline constexpr std::size_t kHeaderSize = 3;

enum class Type : char {
    LoginRequest    = 'L',
    UnsequencedData = 'U',
    ClientHeartbeat = 'R',
    LogoutRequest   = 'O',
    LoginAccepted   = 'A',
    LoginRejected   = 'J',
    SequencedData   = 'S',
    ServerHeartbeat = 'H',
    EndOfSession    = 'Z',
    Debug           = '+',
};

enum class RejectReason : char { NotAuthorized = 'A', SessionUnavailable = 'S' };

struct LoginRequest {
    wire::Alpha<6>  username;
    wire::Alpha<10> password;
    wire::Alpha<10> requestedSession;
    wire::Alpha<20> requestedSequenceNumber;
};
static_assert(sizeof(LoginRequest) == 46);

struct LoginAccepted {
    wire::Alpha<10> session;
    wire::Alpha<20> sequenceNumber;
};
static_assert(sizeof(LoginAccepted) == 30);

struct LoginRejected {
    char rejectReasonCode;
};
static_assert(sizeof(LoginRejected) == 1);

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

struct Packet {
    Type                        type;
    std::span<const std::byte>  payload;
};

[[nodiscard]] inline std::size_t parse(std::span<const std::byte> data, Packet& out) noexcept {
    if (data.size() < kHeaderSize) return 0;
    const std::uint16_t plen = getU16(data.data());
    if (plen == 0) return 0;
    const std::size_t total = 2 + plen;
    if (data.size() < total) return 0;
    out.type = static_cast<Type>(static_cast<char>(data[2]));
    out.payload = data.subspan(kHeaderSize, plen - 1);
    return total;
}

}
