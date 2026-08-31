#include "abt/config/NetParse.hpp"

#include <array>
#include <cstddef>

namespace abt::config {

namespace {

[[nodiscard]] int hexVal(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}   // namespace

net::MacAddr parseMac(std::string_view s) noexcept {
    net::MacAddr mac{};
    std::size_t  idx    = 0;
    unsigned     acc    = 0;
    int          digits = 0;
    const auto   flush  = [&] {
        if (digits > 0 && idx < mac.size()) {
            mac[idx++] = static_cast<std::uint8_t>(acc);
        }
        acc    = 0;
        digits = 0;
    };
    for (const char c : s) {
        if (c == ':' || c == '-') {
            flush();
            continue;
        }
        const int h = hexVal(c);
        if (h < 0) {
            continue;
        }
        acc = acc * 16u + static_cast<unsigned>(h);
        ++digits;
    }
    flush();
    return mac;
}

std::uint32_t parseIp(std::string_view s) noexcept {
    std::array<std::uint8_t, 4> oct{};
    std::size_t                 idx   = 0;
    unsigned                    acc   = 0;
    const auto                  flush = [&] {
        if (idx < oct.size()) {
            oct[idx++] = static_cast<std::uint8_t>(acc);
        }
        acc = 0;
    };
    for (const char c : s) {
        if (c == '.') {
            flush();
            continue;
        }
        if (c >= '0' && c <= '9') {
            acc = acc * 10u + static_cast<unsigned>(c - '0');
        }
    }
    flush();
    return net::ipv4(oct[0], oct[1], oct[2], oct[3]);
}

}   // namespace abt::config
