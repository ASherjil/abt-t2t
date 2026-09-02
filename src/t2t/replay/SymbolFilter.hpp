#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace abt::replay {

class SymbolFilter {
public:
    explicit SymbolFilter(std::string_view symbol);
    explicit SymbolFilter(std::vector<std::string> symbols);

    [[nodiscard]] bool accept(std::span<const std::byte> msg) noexcept;

    [[nodiscard]] bool                            resolved() const noexcept;
    [[nodiscard]] std::uint16_t                   stockLocate() const noexcept;
    [[nodiscard]] const std::string&              symbol() const noexcept;
    [[nodiscard]] const std::vector<std::string>& symbols() const noexcept;
    [[nodiscard]] std::size_t                     resolvedCount() const noexcept;

    [[nodiscard]] static std::uint16_t locateOf(std::span<const std::byte> msg) noexcept;
    [[nodiscard]] static std::uint64_t timestampOf(std::span<const std::byte> msg) noexcept;

private:
    std::vector<std::string>   m_symbols;
    std::vector<std::uint16_t> m_locates;
    std::vector<bool>          m_mine;
    std::size_t                m_resolved = 0;
};

[[nodiscard]] std::string   formatTimeOfDay(std::uint64_t nsSinceMidnight);
[[nodiscard]] std::uint64_t parseTimeOfDay(std::string_view v);

}   // namespace abt::replay
