#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace abt::replay {

class SymbolFilter {
public:
    explicit SymbolFilter(std::string_view symbol);

    [[nodiscard]] bool accept(std::span<const std::byte> msg) noexcept;

    [[nodiscard]] bool               resolved() const noexcept;
    [[nodiscard]] std::uint16_t      stockLocate() const noexcept;
    [[nodiscard]] const std::string& symbol() const noexcept;

    [[nodiscard]] static std::uint16_t locateOf(std::span<const std::byte> msg) noexcept;
    [[nodiscard]] static std::uint64_t timestampOf(std::span<const std::byte> msg) noexcept;

private:
    std::string   m_symbol;
    std::uint16_t m_locate   = 0;
    bool          m_resolved = false;
};

[[nodiscard]] std::string   formatTimeOfDay(std::uint64_t nsSinceMidnight);
[[nodiscard]] std::uint64_t parseTimeOfDay(std::string_view v);

}   // namespace abt::replay
