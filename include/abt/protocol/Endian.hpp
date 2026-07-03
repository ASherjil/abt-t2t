#pragma once
//
// Endian.hpp -- zero-cost big-endian field primitives for wire-protocol overlays.
//
// Exchange protocols (Nasdaq ITCH/OUCH, MoldUDP64, SoupBinTCP) are big-endian and
// wire-packed. We model each field as a 1-byte-aligned wrapper holding the raw
// network-order bytes, with load/store that byteswap on little-endian hosts. Because
// every wrapper is standard-layout and alignof == 1, a message struct built from them
// has no padding and can be laid directly over a received frame (a `reinterpret_cast`
// / `std::memcpy` overlay) with correct decoding and zero copies on the hot path.
//
// C++20 has no std::byteswap (that arrived in C++23), so we provide a constexpr one.
//
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace abt::wire {

// --- constexpr byte swap (compiler intrinsics lower to a single BSWAP / MOVBE) -----
template <std::integral T>
[[nodiscard]] constexpr T bswap(T v) noexcept {
    if constexpr (sizeof(T) == 1) {
        return v;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(__builtin_bswap16(static_cast<std::uint16_t>(v)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(__builtin_bswap32(static_cast<std::uint32_t>(v)));
    } else {
        static_assert(sizeof(T) == 8, "unsupported integer width");
        return static_cast<T>(__builtin_bswap64(static_cast<std::uint64_t>(v)));
    }
}

// --- BigEndian<T> : an integer field stored in network byte order -------------------
template <std::integral T>
struct BigEndian {
    std::array<std::byte, sizeof(T)> bytes;

    [[nodiscard]] T value() const noexcept {
        T host;
        std::memcpy(&host, bytes.data(), sizeof(T));
        if constexpr (std::endian::native == std::endian::little) {
            return bswap(host);
        } else {
            return host;
        }
    }
    void store(T v) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            v = bswap(v);
        }
        std::memcpy(bytes.data(), &v, sizeof(T));
    }

    [[nodiscard]] operator T() const noexcept { return value(); }   // implicit read
    BigEndian& operator=(T v) noexcept { store(v); return *this; }  // typed write
};
static_assert(sizeof(BigEndian<std::uint16_t>) == 2);
static_assert(sizeof(BigEndian<std::uint32_t>) == 4);
static_assert(sizeof(BigEndian<std::uint64_t>) == 8);
static_assert(alignof(BigEndian<std::uint64_t>) == 1);
static_assert(std::is_trivially_copyable_v<BigEndian<std::uint64_t>>);
static_assert(std::is_standard_layout_v<BigEndian<std::uint64_t>>);

// --- Uint48 : a 48-bit big-endian unsigned (ITCH timestamps = ns since midnight) ----
struct Uint48 {
    std::array<std::byte, 6> bytes;

    [[nodiscard]] std::uint64_t value() const noexcept {
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < 6; ++i) {
            v = (v << 8) | std::to_integer<std::uint64_t>(bytes[i]);
        }
        return v;
    }
    void store(std::uint64_t v) noexcept {
        for (std::size_t i = 6; i-- > 0;) {
            bytes[i] = static_cast<std::byte>(v & 0xFFu);
            v >>= 8;
        }
    }
    [[nodiscard]] operator std::uint64_t() const noexcept { return value(); }
    Uint48& operator=(std::uint64_t v) noexcept { store(v); return *this; }
};
static_assert(sizeof(Uint48) == 6);
static_assert(alignof(Uint48) == 1);

// --- Alpha<N> : fixed-width ASCII, left-justified and space-padded (ITCH convention)-
template <std::size_t N>
struct Alpha {
    std::array<char, N> chars;

    // Trailing-space-trimmed logical value (e.g. "AAPL    " -> "AAPL").
    [[nodiscard]] std::string_view view() const noexcept {
        std::size_t len = N;
        while (len > 0 && chars[len - 1] == ' ') {
            --len;
        }
        return {chars.data(), len};
    }
    void store(std::string_view s) noexcept {
        const std::size_t n = s.size() < N ? s.size() : N;
        std::memcpy(chars.data(), s.data(), n);
        for (std::size_t i = n; i < N; ++i) {
            chars[i] = ' ';
        }
    }
    Alpha& operator=(std::string_view s) noexcept { store(s); return *this; }
};
static_assert(sizeof(Alpha<8>) == 8);
static_assert(alignof(Alpha<8>) == 1);

// --- Convenience aliases used throughout the protocol structs -----------------------
using u16be = BigEndian<std::uint16_t>;
using u32be = BigEndian<std::uint32_t>;
using u64be = BigEndian<std::uint64_t>;

}  // namespace abt::wire
