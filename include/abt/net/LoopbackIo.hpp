#pragma once
//
// LoopbackIo.hpp -- an in-memory I/O boundary for the exchange session. Captures the
// MoldUDP64 market-data datagrams and SoupBinTCP order-entry packets the session emits,
// so the whole simulator can be driven and asserted end-to-end without a NIC.
//
// It satisfies the same interface a real transport adapter must provide:
//     void marketDataOut(std::span<const std::byte>);   // one MoldUDP64 datagram
//     void orderEntryOut(std::span<const std::byte>);    // one SoupBinTCP packet
//
#include <cstddef>
#include <span>
#include <vector>

namespace abt::net {

struct LoopbackIo {
    std::vector<std::vector<std::byte>> md;   // MoldUDP64 datagrams
    std::vector<std::vector<std::byte>> oe;   // SoupBinTCP packets

    void marketDataOut(std::span<const std::byte> b) { md.emplace_back(b.begin(), b.end()); }
    void orderEntryOut(std::span<const std::byte> b) { oe.emplace_back(b.begin(), b.end()); }

    void clear() { md.clear(); oe.clear(); }
};

}  // namespace abt::net
