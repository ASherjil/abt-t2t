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
#include "abt/dut/T2tRecorder.hpp"
#include "abt/dut/TxStamp.hpp"
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
    std::size_t   t2tCapacity = 1u << 16;   // tick-to-trade samples retained for percentiles
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

    // Drain a source of asynchronous TX-completion timestamps and close out each pending order's
    // tick-to-trade sample. Mode- and transport-agnostic: the source may be the datapath ring
    // itself or a sidecar, and the test drives it with a mock.
    template <TxStampSource Src>
    void pollTxCompletions(Src& src);
    void completeTx(std::uint32_t userRef, std::uint64_t txHwts) noexcept;

    [[nodiscard]] const BookBuilder& book() const noexcept;
    [[nodiscard]] const T2tRecorder& t2t() const noexcept;
    [[nodiscard]] std::uint32_t ordersSent() const noexcept;

    // Loopback capture of the raw OUCH order bytes that were "sent".
    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrders() const
        requires (Mode == IoMode::Loopback);

private:
    void applyPacket(std::span<const std::byte> moldPacket, std::uint64_t rxHwts);
    void react(std::uint64_t rxHwts);
    [[nodiscard]] std::size_t encodeEnterOrder(const OrderIntent& intent,
                                               std::uint32_t userRef) noexcept;
    [[nodiscard]] bool sendOrder(std::span<const std::byte> ouch);
    void recordSend(std::uint32_t userRef, std::uint64_t rxHwts) noexcept;

    // Pending orders awaiting their TX-completion timestamp, keyed by userRefNum into a fixed ring
    // (userRefs are monotonic and completions arrive within microseconds, so this never wraps in
    // practice). Each slot remembers the RX timestamp that triggered the order.
    static constexpr std::uint32_t kInFlight = 1024;
    struct InFlight {
        std::uint32_t userRef = 0;
        std::uint64_t rxHwts  = 0;
        bool          live    = false;
    };

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
    T2tRecorder   m_t2t;
    std::uint32_t m_nextUserRef;
    std::uint32_t m_ordersSent = 0;

    std::array<InFlight, kInFlight> m_inflight{};
    std::array<std::byte, 128>      m_oeBuf{};

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty> m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty>
        m_io{};
};

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::DutSession(const DutConfig& cfg, Strat strat)
    : m_cfg(cfg),
      m_strat(std::move(strat)),
      m_book(cfg.minPrice, cfg.maxPrice, cfg.tickWire),
      m_t2t(cfg.t2tCapacity),
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
template <TxStampSource Src>
void DutSession<Mode, Strat, Io>::pollTxCompletions(Src& src) {
    for (auto c = src.pollTxTimestamp(); c.status != 0; c = src.pollTxTimestamp()) {
        const std::uint64_t txHwts =
            static_cast<std::uint64_t>(c.sec) * 1'000'000'000ull + c.nsec;
        completeTx(c.userRef, txHwts);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::completeTx(std::uint32_t userRef, std::uint64_t txHwts) noexcept {
    InFlight& slot = m_inflight[userRef % kInFlight];
    if (!slot.live || slot.userRef != userRef) {
        return;
    }
    slot.live = false;
    if (txHwts >= slot.rxHwts) {
        m_t2t.record(txHwts - slot.rxHwts);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
const BookBuilder& DutSession<Mode, Strat, Io>::book() const noexcept {
    return m_book;
}

template <IoMode Mode, Strategy Strat, class Io>
const T2tRecorder& DutSession<Mode, Strat, Io>::t2t() const noexcept {
    return m_t2t;
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
    const std::uint32_t userRef = m_nextUserRef++;
    const std::size_t n = encodeEnterOrder(intent, userRef);
    if (sendOrder({m_oeBuf.data(), n})) {
        // Remember which RX stamp triggered this order; the matching TX-completion stamp closes
        // the tick-to-trade sample when it arrives (see completeTx).
        recordSend(userRef, rxHwts);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
std::size_t DutSession<Mode, Strat, Io>::encodeEnterOrder(const OrderIntent& intent,
                                                          std::uint32_t userRef) noexcept {
    ouch::EnterOrder o{};
    o.type = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum = userRef;
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
bool DutSession<Mode, Strat, Io>::sendOrder(std::span<const std::byte> ouch) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.oe.emplace_back(ouch.begin(), ouch.end());
    } else {
        const auto frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + ouch.size());
        std::uint8_t* buf = m_io.io->acquire(frameLen);
        if (buf == nullptr) [[unlikely]] {
            return false;
        }
        std::memcpy(buf, m_io.oeFramer->header().data(), net::kL2L3L4Overhead);
        std::memcpy(buf + net::kL2L3L4Overhead, ouch.data(), ouch.size());
        m_io.oeFramer->patch(reinterpret_cast<std::byte*>(buf), ouch.size());
        m_io.io->commit();
    }
    ++m_ordersSent;
    return true;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::recordSend(std::uint32_t userRef, std::uint64_t rxHwts) noexcept {
    m_inflight[userRef % kInFlight] = InFlight{userRef, rxHwts, true};
}

}
