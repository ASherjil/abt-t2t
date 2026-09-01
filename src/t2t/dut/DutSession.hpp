#pragma once

#include <array>
#include <cerrno>
#include <concepts>
#include <csignal>
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/core.h>

#include "third_party/abtrda3/RingConcepts.hpp"

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/LatencyRecorder.hpp"
#include "t2t/dut/OrderManager.hpp"
#include "t2t/dut/SequenceTracker.hpp"
#include "t2t/dut/Strategy.hpp"
#include "t2t/dut/TxStamp.hpp"
#include "t2t/lob/Types.hpp"
#include "t2t/protocol/EthIpUdp.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"
#include "t2t/protocol/UdpFramer.hpp"
#include "t2t/util/Clock.hpp"
#include "t2t/util/Tsc.hpp"
#include "t2t/util/UniqueFd.hpp"

namespace abt::dut {

enum class IoMode {
    Loopback,
    Socket,
    Transport
};

struct NoTransport {};

struct DutConfig {
    Price         minPrice = 0;
    Price         maxPrice = 0;
    Price         tickWire = 1;
    std::string   symbol{};
    std::uint16_t stockLocate     = 0;
    bool          marketHoursOnly = false;
    OrderId       ownRefMin       = 0;
    std::uint32_t firstUserRef    = 1;
    std::size_t   maxOrders       = 1u << 12;
    std::size_t   queueCapacity   = 1u << 16;
    int           sigFigs         = 3;
};

template <IoMode Mode, Strategy Strat, class Io = NoTransport>
class DutSession {
public:
// Rule of zero applies here
    DutSession(const DutConfig& cfg, Strat strat);

    void onMarketData(std::span<const std::byte> moldPacket, std::uint64_t rxHwts, std::uint64_t rxTsc = 0)
        requires (Mode == IoMode::Loopback || Mode == IoMode::Socket);
    void onAck(std::span<const std::byte> ouch) noexcept;

    [[nodiscard]] bool connectVenue(const char* oeHost, std::uint16_t oePort, const char* mdBindHost,
                                    std::uint16_t mdPort)
        requires (Mode == IoMode::Socket);
    void attachSockets(util::UniqueFd oeFd, util::UniqueFd mdFd)
        requires (Mode == IoMode::Socket);
    void login(std::string_view session, std::string_view user)
        requires (Mode == IoMode::Socket);
    void onOrderEntry(std::span<const std::byte> data)
        requires (Mode == IoMode::Socket);
    [[nodiscard]] bool sessionEstablished() const noexcept
        requires (Mode == IoMode::Socket);
    template <class Periodic>
    void run(volatile std::sig_atomic_t& stop, Periodic&& periodic)
        requires (Mode == IoMode::Socket);

    [[nodiscard]] bool prepareTransport(Io& io, const net::Endpoints& oeEp, std::uint32_t maxTxFrame = 0)
        requires (Mode == IoMode::Transport && TxRing<Io>);
    void poll()
        requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>);
    void sendLogin(std::string_view session, std::string_view user)
        requires (Mode == IoMode::Transport && TxRing<Io>);
    [[nodiscard]] bool sessionEstablished() const noexcept
        requires (Mode == IoMode::Transport);

    template <TxStampSource Src>
    void pollTxCompletions(Src& src);
    void completeTx(std::uint32_t userRef, std::uint64_t txHwts) noexcept;

    [[nodiscard]] const BookBuilder&     book() const noexcept;
    [[nodiscard]] const OrderManager&    oms() const noexcept;
    [[nodiscard]] const SequenceTracker& feed() const noexcept;
    [[nodiscard]] LatencyRecorder&       t2t() noexcept;
    [[nodiscard]] LatencyRecorder&       t2tSw() noexcept;
    [[nodiscard]] LatencyRecorder&       proc() noexcept;
    [[nodiscard]] std::uint32_t          ordersSent() const noexcept;
    [[nodiscard]] std::uint64_t          packetsReceived() const noexcept;
    [[nodiscard]] std::uint64_t          foreignMessages() const noexcept;
    [[nodiscard]] std::uint32_t          sessionResets() const noexcept;
    [[nodiscard]] bool                   tradingAllowed() const noexcept;

    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrders() const
        requires (Mode == IoMode::Loopback);

private:
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
        std::uint16_t                 ackPort  = 0;
        bool                          loggedIn = false;
    };

    struct SocketState {
        util::UniqueFd             mdFd;
        util::UniqueFd             oeFd;
        std::vector<std::byte>     rx;
        std::array<std::byte, 256> soupBuf{};
        bool                       loggedIn = false;
    };

    void applyPacket(std::span<const std::byte> moldPacket, std::uint64_t rxHwts, std::uint64_t rxTsc);
    void applyMessage(std::span<const std::byte> msg);
    void onSystemEvent(std::span<const std::byte> msg) noexcept;
    [[nodiscard]] bool                 sendOrder(std::span<const std::byte> ouch);
    void                               recordSend(std::uint32_t userRef, std::uint64_t rxHwts) noexcept;
    [[nodiscard]] static std::uint16_t udpDstPort(const std::uint8_t* frame) noexcept;

    DutConfig       m_cfg;
    Strat           m_strat;
    BookBuilder     m_book;
    OrderManager    m_oms;
    SequenceTracker m_seq;
    LatencyRecorder m_t2t;
    LatencyRecorder m_t2tSw;
    LatencyRecorder m_proc;
    std::uint32_t   m_ordersSent = 0;
    std::uint64_t   m_packets    = 0;
    std::uint64_t   m_foreign    = 0;
    std::uint32_t   m_resets     = 0;
    bool            m_marketOpen = false;
    bool            m_trading    = true;

    std::array<Outbound, OrderManager::kMaxOutbound> m_out{};
    std::array<InFlight, kInFlight>                  m_inflight{};

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty>         m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Socket, SocketState, Empty>       m_sock{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty> m_io{};
};

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::DutSession(const DutConfig& cfg, Strat strat)
    : m_cfg(cfg),
      m_strat(std::move(strat)),
      m_book(cfg.minPrice, cfg.maxPrice, cfg.tickWire, cfg.maxOrders, cfg.ownRefMin),
      m_oms(OmsConfig{.symbol = cfg.symbol, .firstUserRef = cfg.firstUserRef}),
      m_t2t("t2t_hw", cfg.queueCapacity, 1.0, cfg.sigFigs),
      m_t2tSw("t2t_sw", cfg.queueCapacity, tsc::nsPerTick(), cfg.sigFigs),
      m_proc("proc", cfg.queueCapacity, tsc::nsPerTick(), cfg.sigFigs) {
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onMarketData(std::span<const std::byte> moldPacket, std::uint64_t rxHwts,
                                               std::uint64_t rxTsc)
    requires (Mode == IoMode::Loopback || Mode == IoMode::Socket)
{
    applyPacket(moldPacket, rxHwts, rxTsc == 0 ? tsc::now() : rxTsc);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onAck(std::span<const std::byte> ouch) noexcept {
    m_oms.onAck(ouch);
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::connectVenue(const char* oeHost, std::uint16_t oePort,
                                               const char* mdBindHost, std::uint16_t mdPort)
    requires (Mode == IoMode::Socket)
{
    m_sock.oeFd.reset(::socket(AF_INET, SOCK_STREAM, 0));
    if (!m_sock.oeFd) {
        fmt::print(stderr, "dut: socket(tcp): {}\n", std::strerror(errno));
        return false;
    }
    sockaddr_in oe{};
    oe.sin_family = AF_INET;
    oe.sin_port   = htons(oePort);
    if (::inet_pton(AF_INET, oeHost, &oe.sin_addr) != 1) {
        fmt::print(stderr, "dut: bad order-entry host {}\n", oeHost);
        return false;
    }
    if (::connect(m_sock.oeFd.get(), reinterpret_cast<sockaddr*>(&oe), sizeof oe) < 0) {
        fmt::print(stderr, "dut: connect({}:{}): {}\n", oeHost, oePort, std::strerror(errno));
        return false;
    }
    int nodelay = 1;
    ::setsockopt(m_sock.oeFd.get(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

    m_sock.mdFd.reset(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!m_sock.mdFd) {
        fmt::print(stderr, "dut: socket(udp): {}\n", std::strerror(errno));
        return false;
    }
    int reuse = 1;
    ::setsockopt(m_sock.mdFd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    sockaddr_in md{};
    md.sin_family = AF_INET;
    md.sin_port   = htons(mdPort);
    if (::inet_pton(AF_INET, mdBindHost, &md.sin_addr) != 1) {
        fmt::print(stderr, "dut: bad market-data bind host {}\n", mdBindHost);
        return false;
    }
    if (::bind(m_sock.mdFd.get(), reinterpret_cast<sockaddr*>(&md), sizeof md) < 0) {
        fmt::print(stderr, "dut: bind({}:{}): {}\n", mdBindHost, mdPort, std::strerror(errno));
        return false;
    }
    return true;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::attachSockets(util::UniqueFd oeFd, util::UniqueFd mdFd)
    requires (Mode == IoMode::Socket)
{
    m_sock.oeFd = std::move(oeFd);
    m_sock.mdFd = std::move(mdFd);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::login(std::string_view session, std::string_view user)
    requires (Mode == IoMode::Socket)
{
    soup::LoginRequest lr{};
    lr.username         = user;
    lr.requestedSession = session;
    const auto pkt      = soup::pack(m_sock.soupBuf.data(), soup::Type::LoginRequest, soup::asBytes(lr));
    (void)::send(m_sock.oeFd.get(), pkt.data(), pkt.size(), MSG_NOSIGNAL);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onOrderEntry(std::span<const std::byte> data)
    requires (Mode == IoMode::Socket)
{
    m_sock.rx.insert(m_sock.rx.end(), data.begin(), data.end());
    std::size_t  off = 0;
    soup::Packet p{};
    for (;;) {
        const std::size_t c = soup::parse({m_sock.rx.data() + off, m_sock.rx.size() - off}, p);
        if (c == 0) {
            break;
        }
        if (p.type == soup::Type::LoginAccepted) {
            m_sock.loggedIn = true;
        } else if (p.type == soup::Type::SequencedData) {
            m_oms.onAck(p.payload);
        }
        off += c;
    }
    if (off != 0) {
        m_sock.rx.erase(m_sock.rx.begin(), m_sock.rx.begin() + static_cast<std::ptrdiff_t>(off));
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::sessionEstablished() const noexcept
    requires (Mode == IoMode::Socket)
{
    return m_sock.loggedIn;
}

template <IoMode Mode, Strategy Strat, class Io>
template <class Periodic>
void DutSession<Mode, Strat, Io>::run(volatile std::sig_atomic_t& stop, Periodic&& periodic)
    requires (Mode == IoMode::Socket)
{
    std::array<std::byte, 8192> rx{};
    while (stop == 0) {
        periodic();
        pollfd pfds[2] = {{m_sock.mdFd.get(), POLLIN, 0}, {m_sock.oeFd.get(), POLLIN, 0}};
        if (::poll(pfds, 2, 1) <= 0) {
            continue;
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            const std::uint64_t rxTsc = tsc::now();
            const ssize_t       n     = ::recv(m_sock.mdFd.get(), rx.data(), rx.size(), 0);
            if (n > 0) {
                onMarketData({rx.data(), static_cast<std::size_t>(n)}, monotonicNs(), rxTsc);
            }
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.oeFd.get(), rx.data(), rx.size(), 0);
            if (n > 0) {
                onOrderEntry({rx.data(), static_cast<std::size_t>(n)});
            } else if (n == 0) {
                fmt::print(stderr, "dut: venue closed order-entry connection\n");
                break;
            }
        }
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::prepareTransport(Io& io, const net::Endpoints& oeEp,
                                                   std::uint32_t maxTxFrame)
    requires (Mode == IoMode::Transport && TxRing<Io>)
{
    constexpr std::uint32_t kMaxOrderFrame = net::kL2L3L4Overhead + Outbound::kSize;
    if (maxTxFrame != 0 && maxTxFrame < kMaxOrderFrame) {
        return false;
    }
    m_io.io = &io;
    m_io.oeFramer.emplace(oeEp);
    m_io.ackPort = oeEp.srcPort;
    return true;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::sendLogin(std::string_view session, std::string_view user)
    requires (Mode == IoMode::Transport && TxRing<Io>)
{
    std::array<std::byte, 64> soupBuf{};
    const auto                pkt      = soup::packLoginRequest(soupBuf.data(), user, session);
    const auto                frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + pkt.size());
    std::uint8_t*             buf      = m_io.io->acquire(frameLen);
    if (buf == nullptr) {
        return;
    }
    std::memcpy(buf, m_io.oeFramer->header().data(), net::kL2L3L4Overhead);
    std::memcpy(buf + net::kL2L3L4Overhead, pkt.data(), pkt.size());
    m_io.oeFramer->patch(reinterpret_cast<std::byte*>(buf), pkt.size());
    m_io.io->commit();
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::sessionEstablished() const noexcept
    requires (Mode == IoMode::Transport)
{
    return m_io.loggedIn;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::poll()
    requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>)
{
    for (;;) {
        const std::uint64_t rxTsc = tsc::now();
        const auto          f     = m_io.io->tryReceive();
        if (f.status == 0) {
            break;
        }
        const auto raw = f.data;
        if (raw.size() > net::kL2L3L4Overhead) {
            const auto*                      frame = reinterpret_cast<const std::uint8_t*>(raw.data());
            const auto*                      p     = reinterpret_cast<const std::byte*>(raw.data());
            const std::span<const std::byte> payload{p + net::kL2L3L4Overhead,
                                                     raw.size() - net::kL2L3L4Overhead};
            if (udpDstPort(frame) == m_io.ackPort) {
                if (payload[0] == std::byte{0}) [[unlikely]] {
                    soup::Packet sp{};
                    if (soup::parse(payload, sp) != 0 && sp.type == soup::Type::LoginAccepted) {
                        m_io.loggedIn = true;
                    }
                } else {
                    m_oms.onAck(payload);
                }
            } else {
                const std::uint64_t rxHwts = static_cast<std::uint64_t>(f.sec) * 1'000'000'000ull + f.nsec;
                applyPacket(payload, rxHwts, rxTsc);
            }
        }
        m_io.io->release();
    }
}

template <IoMode Mode, Strategy Strat, class Io>
template <TxStampSource Src>
void DutSession<Mode, Strat, Io>::pollTxCompletions(Src& src) {
    for (auto c = src.pollTxTimestamp(); c.status != 0; c = src.pollTxTimestamp()) {
        const std::uint64_t txHwts = static_cast<std::uint64_t>(c.sec) * 1'000'000'000ull + c.nsec;
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
const OrderManager& DutSession<Mode, Strat, Io>::oms() const noexcept {
    return m_oms;
}

template <IoMode Mode, Strategy Strat, class Io>
const SequenceTracker& DutSession<Mode, Strat, Io>::feed() const noexcept {
    return m_seq;
}

template <IoMode Mode, Strategy Strat, class Io>
LatencyRecorder& DutSession<Mode, Strat, Io>::t2t() noexcept {
    return m_t2t;
}

template <IoMode Mode, Strategy Strat, class Io>
LatencyRecorder& DutSession<Mode, Strat, Io>::t2tSw() noexcept {
    return m_t2tSw;
}

template <IoMode Mode, Strategy Strat, class Io>
LatencyRecorder& DutSession<Mode, Strat, Io>::proc() noexcept {
    return m_proc;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint32_t DutSession<Mode, Strat, Io>::ordersSent() const noexcept {
    return m_ordersSent;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::packetsReceived() const noexcept {
    return m_packets;
}

template <IoMode Mode, Strategy Strat, class Io>
const std::vector<std::vector<std::byte>>& DutSession<Mode, Strat, Io>::capturedOrders() const
    requires (Mode == IoMode::Loopback)
{
    return m_cap.oe;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::applyPacket(std::span<const std::byte> moldPacket, std::uint64_t rxHwts,
                                              std::uint64_t rxTsc) {
    if (moldPacket.size() < mold::kHeaderSize) [[unlikely]] {
        return;
    }
    ++m_packets;
    const std::uint64_t           begin = tsc::now();
    const SequenceTracker::Result r = m_seq.onPacket(mold::sequenceOf(moldPacket), mold::countOf(moldPacket));
    if (r == SequenceTracker::Result::Stale) [[unlikely]] {
        return;
    }
    mold::forEachMessage(moldPacket, [this](std::uint64_t, std::span<const std::byte> msg) {
        applyMessage(msg);
    });
    const QuoteTargets  targets = tradingAllowed() ? m_strat.onBook(m_book, m_oms.account()) : QuoteTargets{};
    const std::size_t   n = m_oms.reconcile(targets, std::span<Outbound, OrderManager::kMaxOutbound>{m_out});
    const std::uint64_t end = tsc::now();
    m_proc.record(end - begin);

    for (std::size_t i = 0; i < n; ++i) {
        if (sendOrder({m_out[i].buf.data(), m_out[i].len}) && i == 0) {
            m_t2tSw.record(tsc::now() - rxTsc);
            recordSend(m_out[i].userRef, rxHwts);
        }
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::applyMessage(std::span<const std::byte> msg) {
    if (msg.size() < 11) [[unlikely]] {
        return;
    }
    const char type = static_cast<char>(msg[0]);
    if (type == 'S') [[unlikely]] {
        onSystemEvent(msg);
        return;
    }
    if (m_cfg.stockLocate != 0) {
        const auto locate = static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                                       std::to_integer<unsigned>(msg[2]));
        if (locate != m_cfg.stockLocate) {
            ++m_foreign;
            return;
        }
    }
    if (type == 'H') [[unlikely]] {
        if (msg.size() >= sizeof(itch::StockTradingAction)) {
            const auto* h = reinterpret_cast<const itch::StockTradingAction*>(msg.data());
            m_trading     = h->tradingState == static_cast<char>(itch::TradingState::Trading);
        }
        return;
    }
    m_book.apply(msg);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onSystemEvent(std::span<const std::byte> msg) noexcept {
    if (msg.size() < sizeof(itch::SystemEvent)) {
        return;
    }
    const auto* s = reinterpret_cast<const itch::SystemEvent*>(msg.data());
    switch (static_cast<itch::SystemEventCode>(s->eventCode)) {
        case itch::SystemEventCode::StartOfMessages:
            m_book.clear();
            m_marketOpen = false;
            m_trading    = true;
            ++m_resets;
            break;
        case itch::SystemEventCode::StartOfMarketHours:
            m_marketOpen = true;
            break;
        case itch::SystemEventCode::EndOfMarketHours:
            m_marketOpen = false;
            break;
        default:
            break;
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::tradingAllowed() const noexcept {
    return !m_cfg.marketHoursOnly || (m_marketOpen && m_trading);
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::foreignMessages() const noexcept {
    return m_foreign;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint32_t DutSession<Mode, Strat, Io>::sessionResets() const noexcept {
    return m_resets;
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::sendOrder(std::span<const std::byte> ouch) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.oe.emplace_back(ouch.begin(), ouch.end());
    } else if constexpr (Mode == IoMode::Socket) {
        const auto pkt = soup::packUnsequencedData(m_sock.soupBuf.data(), ouch);
        if (::send(m_sock.oeFd.get(), pkt.data(), pkt.size(), MSG_NOSIGNAL) <= 0) {
            return false;
        }
    } else {
        const auto    frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + ouch.size());
        std::uint8_t* buf      = m_io.io->acquire(frameLen);
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

template <IoMode Mode, Strategy Strat, class Io>
std::uint16_t DutSession<Mode, Strat, Io>::udpDstPort(const std::uint8_t* frame) noexcept {
    constexpr std::size_t off = net::kEthHeaderSize + net::kIpv4HeaderSize + 2;
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[off]) << 8) | frame[off + 1]);
}

}   // namespace abt::dut
