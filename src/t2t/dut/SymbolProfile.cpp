#include "t2t/dut/SymbolProfile.hpp"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <string_view>

#include <fmt/core.h>

namespace abt::dut {

namespace {

std::string_view nextField(std::string_view& line) noexcept {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    std::size_t n = 0;
    while (n < line.size() && line[n] != ' ' && line[n] != '\t') {
        ++n;
    }
    const std::string_view field = line.substr(0, n);
    line.remove_prefix(n);
    return field;
}

template <class T>
bool parseInto(std::string_view s, T& out) noexcept {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

}   // namespace

std::vector<SymbolProfile> readSymbolProfile(const std::string& path) {
    std::vector<SymbolProfile> rows;
    std::ifstream              in(path);
    if (!in) {
        return rows;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string_view rest = line;
        if (rest.empty() || rest.front() == '#') {
            continue;
        }
        SymbolProfile          p;
        const std::string_view name = nextField(rest);
        if (name.empty() || !parseInto(nextField(rest), p.refPrice) ||
            !parseInto(nextField(rest), p.peakOrders)) {
            continue;
        }
        p.name = std::string(name);
        rows.push_back(std::move(p));
    }
    return rows;
}

bool writeSymbolProfile(const std::string& path, std::span<const SymbolProfile> rows) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        return false;
    }
    fmt::print(f, "# abt-t2t symbol profile: name reference_price_wire peak_live_orders\n");
    for (const SymbolProfile& p : rows) {
        fmt::print(f, "{} {} {}\n", p.name, p.refPrice, p.peakOrders);
    }
    return std::fclose(f) == 0;
}

}   // namespace abt::dut
