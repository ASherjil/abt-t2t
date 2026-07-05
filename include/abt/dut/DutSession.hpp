#pragma once
//
// DUT session: the latency-critical tick-to-trade loop. Market data arrives (MoldUDP64 carrying
// ITCH), the book is rebuilt, a compile-time Strategy reacts, and — if it fires — an OUCH order
// is encoded and sent. A compile-time IoMode picks in-memory capture (Loopback, for tests) or a
// ring transport (Transport) supplied through the Io type parameter, which must satisfy the
// vendored ABTRDA3 TxRing/RxRing concepts (ef_vi / DPDK / AF_XDP back-ends all do). This mirrors
// the exchange sim's ExchangeSession, but flows the other way: RX feed -> book -> TX order.
//

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "third_party/abtrda3/RingConcepts.hpp"

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/Strategy.hpp"
#include "abt/lob/Types.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/UdpFramer.hpp"

namespace abt::dut {

enum class IoMode { Loopback, Transport };

// Sentinel for the unused Io parameter in Loopback mode (a complete empty type, so no reference-
// to-void hazard when the transport members are conditioned out).
struct NoTransport {};

struct DutConfig {
    Price         minPrice = 0;   // book band, in wire prices
    Price         maxPrice = 0;
    Price         tickWire = 1;
    std::string   symbol{};       // OUCH symbol stamped on every order
    std::uint32_t firstUserRef = 1;
};

template <IoMode Mode, Strategy Strat, class Io = NoTransport>
class DutSession {
public:
    DutSession(const DutConfig& cfg, Strat strat);

    // Loopback: hand the pipeline one raw MoldUDP64 packet plus its RX hardware timestamp.
    void onMarketData(std::span<const std::byte> moldPacket, std::uint64_t rxHwts)
        requires (Mode == IoMode::Loopback);

    // Transport: bind the ring and order-entry framing, then drain RX and react on each poll.
    void prepareTransport(Io& io, const net::Endpoints& oeEp)
        requires (Mode == IoMode::Transport && TxRing<Io>);
    void poll() requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>);

    [[nodiscard]] const BookBuilder& book() const noexcept;
    [[nodiscard]] std::uint32_t ordersSent() const noexcept;

    // Loopback capture of the raw OUCH order bytes that were "sent".
    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrders() const
        requires (Mode == IoMode::Loopback);

private:
    void applyPacket(std::span<const std::byte> moldPacket, std::uint64_t rxHwts);
    void react(std::uint64_t rxHwts);
    [[nodiscard]] std::size_t encodeEnterOrder(const OrderIntent& intent) noexcept;
    void sendOrder(std::span<const std::byte> ouch);

    struct Capture {
        std::vector<std::vector<std::byte>> oe;
    };
    struct Empty {};
    struct TransportState {
        Io*                           io = nullptr;
        std::optional<net::UdpFramer> oeFramer;
    };

    DutConfig     m_cfg;
    Strat         m_strat;
    BookBuilder   m_book;
    std::uint32_t m_nextUserRef;
    std::uint32_t m_ordersSent = 0;

    std::array<std::byte, 128> m_oeBuf{};

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty> m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty>
        m_io{};
};

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::DutSession(const DutConfig& cfg, Strat strat)
    : m_cfg(cfg),
      m_strat(std::move(strat)),
      m_book(cfg.minPrice, cfg.maxPrice, cfg.tickWire),
      m_nextUserRef(cfg.firstUserRef) {
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onMarketData(std::span<const std::byte> moldPacket,
                                               std::uint64_t rxHwts)
    requires (Mode == IoMode::Loopback) {
    applyPacket(moldPacket, rxHwts);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::prepareTransport(Io& io, const net::Endpoints& oeEp)
    requires (Mode == IoMode::Transport && TxRing<Io>) {
    m_io.io = &io;
    m_io.oeFramer.emplace(oeEp);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::poll()
    requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>) {
    for (auto f = m_io.io->tryReceive(); f.status != 0; f = m_io.io->tryReceive()) {
        const auto raw = f.data;
        if (raw.size() > net::kL2L3L4Overhead) {
            const auto* p = reinterpret_cast<const std::byte*>(raw.data());
            const std::uint64_t rxHwts =
                static_cast<std::uint64_t>(f.sec) * 1'000'000'000ull + f.nsec;
            applyPacket({p + net::kL2L3L4Overhead, raw.size() - net::kL2L3L4Overhead}, rxHwts);
        }
        m_io.io->release();
    }
}

template <IoMode Mode, Strategy Strat, class Io>
const BookBuilder& DutSession<Mode, Strat, Io>::book() const noexcept {
    return m_book;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint32_t DutSession<Mode, Strat, Io>::ordersSent() const noexcept {
    return m_ordersSent;
}

template <IoMode Mode, Strategy Strat, class Io>
const std::vector<std::vector<std::byte>>& DutSession<Mode, Strat, Io>::capturedOrders() const
    requires (Mode == IoMode::Loopback) {
    return m_cap.oe;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::applyPacket(std::span<const std::byte> moldPacket,
                                              std::uint64_t rxHwts) {
    mold::forEachMessage(moldPacket,
        [this](std::uint64_t, std::span<const std::byte> msg) {
            m_book.apply(msg);
        });
    react(rxHwts);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::react(std::uint64_t rxHwts) {
    const OrderIntent intent = m_strat.onBook(m_book);
    if (!intent.send) {
        return;
    }
    const std::size_t n = encodeEnterOrder(intent);
    sendOrder({m_oeBuf.data(), n});
    // The RX timestamp pairs with the TX timestamp for the tick-to-trade delta; the TX side is
    // net-new API surface (it does not fit TxRing) and lands with the t2t harness in a later chunk.
    (void)rxHwts;
}

template <IoMode Mode, Strategy Strat, class Io>
std::size_t DutSession<Mode, Strat, Io>::encodeEnterOrder(const OrderIntent& intent) noexcept {
    ouch::EnterOrder o{};
    o.type = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum = m_nextUserRef++;
    o.side = (intent.side == Side::Buy) ? static_cast<char>(ouch::Side::Buy)
                                        : static_cast<char>(ouch::Side::Sell);
    o.quantity = intent.qty;
    o.symbol = std::string_view{m_cfg.symbol};
    o.price = static_cast<std::uint64_t>(static_cast<std::uint32_t>(intent.price));
    o.timeInForce = static_cast<char>(ouch::TimeInForce::Day);
    o.display = static_cast<char>(ouch::Display::Visible);
    o.capacity = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType = static_cast<char>(ouch::CrossType::Continuous);
    o.appendageLength = 0;
    std::memcpy(m_oeBuf.data(), &o, sizeof o);
    return sizeof o;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::sendOrder(std::span<const std::byte> ouch) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.oe.emplace_back(ouch.begin(), ouch.end());
    } else {
        const auto frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + ouch.size());
        std::uint8_t* buf = m_io.io->acquire(frameLen);
        if (buf == nullptr) [[unlikely]] {
            return;
        }
        std::memcpy(buf, m_io.oeFramer->header().data(), net::kL2L3L4Overhead);
        std::memcpy(buf + net::kL2L3L4Overhead, ouch.data(), ouch.size());
        m_io.oeFramer->patch(reinterpret_cast<std::byte*>(buf), ouch.size());
        m_io.io->commit();
    }
    ++m_ordersSent;
}

}
