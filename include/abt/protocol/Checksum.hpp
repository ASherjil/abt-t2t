#pragma once

// Internet checksum (RFC 1071): one's-complement 16-bit sum — the IPv4 header check.

#include <cstddef>
#include <cstdint>
#include <span>

namespace abt::net {

    [[nodiscard]] constexpr std::uint16_t computeChecksum(std::span<const std::byte> data) noexcept {
        std::uint32_t sum = 0;
        std::size_t i = 0;
        for (/* i declared above */; (i + 1) < data.size(); i +=2) {
            sum += (std::to_integer<std::uint32_t>(data[i]) << 8) | std::to_integer<std::uint32_t>(data[i+1]);
        }
        if (i < data.size()) {
            sum += std::to_integer<std::uint32_t>(data[i]) << 8;
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
            return static_cast<std::uint16_t>(~sum);
    }
}