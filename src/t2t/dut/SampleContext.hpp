#pragma once

#include <cstdint>

namespace abt::dut {

struct SampleContext {
    static constexpr std::uint8_t kSent   = 1u << 0;
    static constexpr std::uint8_t kRehash = 1u << 1;
    static constexpr std::uint8_t kGap    = 1u << 2;

    [[nodiscard]] static constexpr std::uint64_t pack(std::uint64_t seq, std::uint16_t msgs,
                                                      std::uint8_t flags) noexcept {
        const std::uint64_t m = msgs > 255u ? 255u : msgs;
        return (seq << 16) | (m << 8) | flags;
    }

    [[nodiscard]] static constexpr std::uint64_t seq(std::uint64_t ctx) noexcept {
        return ctx >> 16;
    }

    [[nodiscard]] static constexpr std::uint8_t msgs(std::uint64_t ctx) noexcept {
        return static_cast<std::uint8_t>((ctx >> 8) & 0xffu);
    }

    [[nodiscard]] static constexpr std::uint8_t flags(std::uint64_t ctx) noexcept {
        return static_cast<std::uint8_t>(ctx & 0xffu);
    }
};

}   // namespace abt::dut
