#pragma once
//
// In-memory ExchangeSession I/O boundary that captures emitted datagrams/packets (tests).
//

#include <cstddef>
#include <span>
#include <vector>

namespace abt::net {

struct LoopbackIo {
    std::vector<std::vector<std::byte>> md;
    std::vector<std::vector<std::byte>> oe;

    void marketDataOut(std::span<const std::byte> b) { md.emplace_back(b.begin(), b.end()); }
    void orderEntryOut(std::span<const std::byte> b) { oe.emplace_back(b.begin(), b.end()); }

    void clear() { md.clear(); oe.clear(); }
};

}
