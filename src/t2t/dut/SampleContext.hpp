#pragma once

#include <cstddef>
#include <cstdint>

namespace abt::dut {

struct SampleContext {
    static constexpr std::uint8_t kSent     = 1u << 0;
    static constexpr std::uint8_t kRehash   = 1u << 1;
    static constexpr std::uint8_t kGap      = 1u << 2;
    static constexpr std::uint8_t kReanchor = 1u << 3;
    static constexpr std::uint8_t kNewBook  = 1u << 4;
    static constexpr std::uint8_t kRescan   = 1u << 5;
    static constexpr std::uint8_t kMulti    = 1u << 6;
    static constexpr std::uint8_t kTxReap   = 1u << 7;

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

    static constexpr std::size_t kStages = 4;

    [[nodiscard]] static constexpr std::uint64_t clampStage(std::uint64_t v) noexcept {
        return v > 0xffffu ? 0xffffu : v;
    }

    [[nodiscard]] static constexpr std::uint64_t packStages(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                                            std::uint64_t d) noexcept {
        return clampStage(a) | (clampStage(b) << 16) | (clampStage(c) << 32) | (clampStage(d) << 48);
    }

    [[nodiscard]] static constexpr std::uint64_t stage(std::uint64_t stages, std::size_t i) noexcept {
        return (stages >> (16 * i)) & 0xffffu;
    }
};

}   // namespace abt::dut
