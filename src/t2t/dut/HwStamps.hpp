#pragma once

#include <concepts>
#include <cstdint>
#include <optional>

namespace abt::dut {

template <typename S>
concept HwStampSource = requires (S s, std::uint64_t packed, std::uint32_t correction) {
    {
        s.hwRxTimestamp()
    } noexcept -> std::same_as<std::uint32_t>;
    {
        s.hwRxTimestampCorrection()
    } noexcept -> std::same_as<std::uint32_t>;
    {
        s.txSequence()
    } noexcept -> std::same_as<std::uint32_t>;
    {
        s.pollTxTimestamp()->seq
    } -> std::convertible_to<std::uint32_t>;
    {
        s.pollTxTimestamp()->minor
    } -> std::convertible_to<std::uint32_t>;
    {
        s.rxFrameComplete()
    } noexcept -> std::same_as<bool>;
    {
        S::hwTurnaround(packed, correction)
    } noexcept -> std::same_as<std::optional<std::uint64_t>>;
};

[[nodiscard]] constexpr std::uint64_t packHwStamps(std::uint32_t rxRaw, std::uint32_t txMinor) noexcept {
    return (static_cast<std::uint64_t>(rxRaw) << 32) | txMinor;
}

template <HwStampSource S>
[[nodiscard]] std::int64_t hwStampsToNs(std::uint64_t packed, std::uint64_t correction) noexcept {
    const std::optional<std::uint64_t> qns = S::hwTurnaround(packed, static_cast<std::uint32_t>(correction));
    return qns ? static_cast<std::int64_t>(*qns / 4) : -1;
}

}   // namespace abt::dut
