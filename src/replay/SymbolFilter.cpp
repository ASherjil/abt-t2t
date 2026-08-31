#include "abt/replay/SymbolFilter.hpp"

#include <cstdio>
#include <string>

#include "abt/protocol/Itch50.hpp"

namespace abt::replay {

SymbolFilter::SymbolFilter(std::string_view symbol)
    : m_symbol(symbol) {
}

bool SymbolFilter::accept(std::span<const std::byte> msg) noexcept {
    if (msg.size() < 11) {
        return false;
    }
    const char type = static_cast<char>(msg[0]);
    if (type == 'S') {
        return true;
    }
    if (type == 'R') {
        if (msg.size() < sizeof(itch::StockDirectory)) {
            return false;
        }
        const auto* r = reinterpret_cast<const itch::StockDirectory*>(msg.data());
        if (r->stock.view() == m_symbol) {
            m_locate = locateOf(msg);
            m_resolved = true;
            return true;
        }
        return false;
    }
    return m_resolved && locateOf(msg) == m_locate;
}

bool SymbolFilter::resolved() const noexcept {
    return m_resolved;
}

std::uint16_t SymbolFilter::stockLocate() const noexcept {
    return m_locate;
}

const std::string& SymbolFilter::symbol() const noexcept {
    return m_symbol;
}

std::uint16_t SymbolFilter::locateOf(std::span<const std::byte> msg) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                      std::to_integer<unsigned>(msg[2]));
}

std::uint64_t SymbolFilter::timestampOf(std::span<const std::byte> msg) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 5; i < 11; ++i) {
        v = (v << 8) | std::to_integer<std::uint64_t>(msg[i]);
    }
    return v;
}

std::string formatTimeOfDay(std::uint64_t ns) {
    const std::uint64_t s = ns / 1'000'000'000ull;
    const std::uint64_t ms = (ns / 1'000'000ull) % 1000ull;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02llu:%02llu:%02llu.%03llu",
                  static_cast<unsigned long long>(s / 3600), static_cast<unsigned long long>((s / 60) % 60),
                  static_cast<unsigned long long>(s % 60), static_cast<unsigned long long>(ms));
    return buf;
}

std::uint64_t parseTimeOfDay(std::string_view v) {
    unsigned h = 0;
    unsigned m = 0;
    unsigned s = 0;
    unsigned ms = 0;
    if (v.empty()) {
        return 0;
    }
    const int n = std::sscanf(std::string(v).c_str(), "%u:%u:%u.%u", &h, &m, &s, &ms);
    if (n < 2) {
        return 0;
    }
    return (static_cast<std::uint64_t>(h) * 3600ull + m * 60ull + s) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ms) * 1'000'000ull;
}

}
