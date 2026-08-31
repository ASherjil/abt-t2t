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

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/LatencyRecorder.hpp"
#include "abt/dut/OrderManager.hpp"
#include "abt/dut/SequenceTracker.hpp"
#include "abt/dut/Strategy.hpp"
#include "abt/dut/TxStamp.hpp"
#include "abt/lob/Types.hpp"
#include "abt/protocol/EthIpUdp.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/protocol/UdpFramer.hpp"
#include "abt/util/Clock.hpp"
#include "abt/util/Tsc.hpp"

namespace abt::dut {

enum class IoMode { Loopback, Socket, Transport };

struct NoTransport {};

struct DutConfig {
    Price         minPrice      = 0;
    Price         maxPrice      = 0;
    Price         tickWire      = 1;
    std::string   symbol{};
    std::uint32_t firstUserRef  = 1;
    std::size_t   maxOrders     = 1u << 12;
    std::size_t   queueCapacity = 1u << 16;
    int           sigFigs       = 3;
};

template <IoMode Mode, Strategy Strat, class Io = NoTransport>
class DutSession {
public:
    DutSession(const DutConfig& cfg, Strat strat);
    ~DutSession();
    DutSession(const DutSession&) = delete;
    DutSession& operator=(const DutSession&) = delete;

    void onMarketData(std::span<const std::byte> moldPacket, std::uint64_t rxHwts)
        requires (Mode == IoMode::Loopback || Mode == IoMode::Socket);
    void onAck(std::span<const std::byte> ouch) noexcept;

    [[nodiscard]] bool connectVenue(const char* oeHost, std::uint16_t oePort,
                                    const char* mdBindHost, std::uint16_t mdPort)
        requires (Mode == IoMode::Socket);
    void attachSockets(int oeFd, int mdFd) requires (Mode == IoMode::Socket);
    void login(std::string_view session, std::string_view user) requires (Mode == IoMode::Socket);
    void onOrderEntry(std::span<const std::byte> data) requires (Mode == IoMode::Socket);
    [[nodiscard]] bool sessionEstablished() const noexcept requires (Mode == IoMode::Socket);
    template <class Periodic>
    void run(volatile std::sig_atomic_t& stop, Periodic&& periodic) requires (Mode == IoMode::Socket);

    void prepareTransport(Io& io, const net::Endpoints& oeEp)
        requires (Mode == IoMode::Transport && TxRing<Io>);
    void poll() requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>);

    template <TxStampSource Src>
    void pollTxCompletions(Src& src);
    void completeTx(std::uint32_t userRef, std::uint64_t txHwts) noexcept;

    [[nodiscard]] const BookBuilder& book() const noexcept;
    [[nodiscard]] const OrderManager& oms() const noexcept;
    [[nodiscard]] const SequenceTracker& feed() const noexcept;
    [[nodiscard]] LatencyRecorder& t2t() noexcept;
    [[nodiscard]] LatencyRecorder& proc() noexcept;
    [[nodiscard]] std::uint32_t ordersSent() const noexcept;
    [[nodiscard]] std::uint64_t packetsReceived() const noexcept;

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
        std::uint16_t                 ackPort = 0;
    };
    struct SocketState {
        int                        mdFd = -1;
        int                        oeFd = -1;
        std::vector<std::byte>     rx;
        std::array<std::byte, 256> soupBuf{};
        bool                       loggedIn = false;
    };

    void applyPacket(std::span<const std::byte> moldPacket, std::uint64_t rxHwts);
    [[nodiscard]] bool sendOrder(std::span<const std::byte> ouch);
    void recordSend(std::uint32_t userRef, std::uint64_t rxHwts) noexcept;
    [[nodiscard]] static std::uint16_t udpDstPort(const std::uint8_t* frame) noexcept;

    DutConfig       m_cfg;
    Strat           m_strat;
    BookBuilder     m_book;
    OrderManager    m_oms;
    SequenceTracker m_seq;
    LatencyRecorder m_t2t;
    LatencyRecorder m_proc;
    std::uint32_t   m_ordersSent = 0;
    std::uint64_t   m_packets    = 0;

    std::array<Outbound, OrderManager::kMaxOutbound> m_out{};
    std::array<InFlight, kInFlight>                  m_inflight{};

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty> m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Socket, SocketState, Empty> m_sock{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty>
        m_io{};
};

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::DutSession(const DutConfig& cfg, Strat strat)
    : m_cfg(cfg),
      m_strat(std::move(strat)),
      m_book(cfg.minPrice, cfg.maxPrice, cfg.tickWire, cfg.maxOrders),
      m_oms(OmsConfig{cfg.symbol, cfg.firstUserRef}),
      m_t2t("t2t", cfg.queueCapacity, 1.0, cfg.sigFigs),
      m_proc("proc", cfg.queueCapacity, tsc::nsPerTick(), cfg.sigFigs) {
}

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::~DutSession() {
    if constexpr (Mode == IoMode::Socket) {
        if (m_sock.oeFd >= 0) {
            ::close(m_sock.oeFd);
        }
        if (m_sock.mdFd >= 0) {
            ::close(m_sock.mdFd);
        }
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onMarketData(std::span<const std::byte> moldPacket,
                                               std::uint64_t rxHwts)
    requires (Mode == IoMode::Loopback || Mode == IoMode::Socket) {
    applyPacket(moldPacket, rxHwts);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onAck(std::span<const std::byte> ouch) noexcept {
    m_oms.onAck(ouch);
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::connectVenue(const char* oeHost, std::uint16_t oePort,
                                               const char* mdBindHost, std::uint16_t mdPort)
    requires (Mode == IoMode::Socket) {
    m_sock.oeFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock.oeFd < 0) {
        fmt::print(stderr, "dut: socket(tcp): {}\n", std::strerror(errno));
        return false;
    }
    sockaddr_in oe{};
    oe.sin_family = AF_INET;
    oe.sin_port = htons(oePort);
    if (::inet_pton(AF_INET, oeHost, &oe.sin_addr) != 1) {
        fmt::print(stderr, "dut: bad order-entry host {}\n", oeHost);
        return false;
    }
    if (::connect(m_sock.oeFd, reinterpret_cast<sockaddr*>(&oe), sizeof oe) < 0) {
        fmt::print(stderr, "dut: connect({}:{}): {}\n", oeHost, oePort, std::strerror(errno));
        return false;
    }
    int nodelay = 1;
    ::setsockopt(m_sock.oeFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

    m_sock.mdFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock.mdFd < 0) {
        fmt::print(stderr, "dut: socket(udp): {}\n", std::strerror(errno));
        return false;
    }
    int reuse = 1;
    ::setsockopt(m_sock.mdFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    sockaddr_in md{};
    md.sin_family = AF_INET;
    md.sin_port = htons(mdPort);
    if (::inet_pton(AF_INET, mdBindHost, &md.sin_addr) != 1) {
        fmt::print(stderr, "dut: bad market-data bind host {}\n", mdBindHost);
        return false;
    }
    if (::bind(m_sock.mdFd, reinterpret_cast<sockaddr*>(&md), sizeof md) < 0) {
        fmt::print(stderr, "dut: bind({}:{}): {}\n", mdBindHost, mdPort, std::strerror(errno));
        return false;
    }
    return true;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::attachSockets(int oeFd, int mdFd)
    requires (Mode == IoMode::Socket) {
    m_sock.oeFd = oeFd;
    m_sock.mdFd = mdFd;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::login(std::string_view session, std::string_view user)
    requires (Mode == IoMode::Socket) {
    soup::LoginRequest lr{};
    lr.username = user;
    lr.requestedSession = session;
    const auto pkt = soup::pack(m_sock.soupBuf.data(), soup::Type::LoginRequest, soup::asBytes(lr));
    (void)::send(m_sock.oeFd, pkt.data(), pkt.size(), MSG_NOSIGNAL);
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onOrderEntry(std::span<const std::byte> data)
    requires (Mode == IoMode::Socket) {
    m_sock.rx.insert(m_sock.rx.end(), data.begin(), data.end());
    std::size_t off = 0;
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
    requires (Mode == IoMode::Socket) {
    return m_sock.loggedIn;
}

template <IoMode Mode, Strategy Strat, class Io>
template <class Periodic>
void DutSession<Mode, Strat, Io>::run(volatile std::sig_atomic_t& stop, Periodic&& periodic)
    requires (Mode == IoMode::Socket) {
    std::array<std::byte, 8192> rx{};
    while (stop == 0) {
        periodic();
        pollfd pfds[2] = {{m_sock.mdFd, POLLIN, 0}, {m_sock.oeFd, POLLIN, 0}};
        if (::poll(pfds, 2, 1) <= 0) {
            continue;
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.mdFd, rx.data(), rx.size(), 0);
            if (n > 0) {
                onMarketData({rx.data(), static_cast<std::size_t>(n)}, monotonicNs());
            }
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.oeFd, rx.data(), rx.size(), 0);
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
void DutSession<Mode, Strat, Io>::prepareTransport(Io& io, const net::Endpoints& oeEp)
    requires (Mode == IoMode::Transport && TxRing<Io>) {
    m_io.io = &io;
    m_io.oeFramer.emplace(oeEp);
    m_io.ackPort = oeEp.srcPort;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::poll()
    requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>) {
    for (auto f = m_io.io->tryReceive(); f.status != 0; f = m_io.io->tryReceive()) {
        const auto raw = f.data;
        if (raw.size() > net::kL2L3L4Overhead) {
            const auto* frame = reinterpret_cast<const std::uint8_t*>(raw.data());
            const auto* p = reinterpret_cast<const std::byte*>(raw.data());
            const std::span<const std::byte> payload{p + net::kL2L3L4Overhead,
                                                     raw.size() - net::kL2L3L4Overhead};
            if (udpDstPort(frame) == m_io.ackPort) {
                m_oms.onAck(payload);
            } else {
                const std::uint64_t rxHwts =
                    static_cast<std::uint64_t>(f.sec) * 1'000'000'000ull + f.nsec;
                applyPacket(payload, rxHwts);
            }
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
    requires (Mode == IoMode::Loopback) {
    return m_cap.oe;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::applyPacket(std::span<const std::byte> moldPacket,
                                              std::uint64_t rxHwts) {
    if (moldPacket.size() < mold::kHeaderSize) [[unlikely]] {
        return;
    }
    ++m_packets;
    const std::uint64_t begin = tsc::now();
    const SequenceTracker::Result r =
        m_seq.onPacket(mold::sequenceOf(moldPacket), mold::countOf(moldPacket));
    if (r == SequenceTracker::Result::Stale) [[unlikely]] {
        return;
    }
    mold::forEachMessage(moldPacket,
        [this](std::uint64_t, std::span<const std::byte> msg) {
            m_book.apply(msg);
        });
    const QuoteTargets targets = m_strat.onBook(m_book, m_oms.account());
    const std::size_t n = m_oms.reconcile(targets, std::span<Outbound, OrderManager::kMaxOutbound>{m_out});
    const std::uint64_t end = tsc::now();
    m_proc.record(end - begin);

    for (std::size_t i = 0; i < n; ++i) {
        if (sendOrder({m_out[i].buf.data(), m_out[i].len}) && i == 0) {
            recordSend(m_out[i].userRef, rxHwts);
        }
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::sendOrder(std::span<const std::byte> ouch) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.oe.emplace_back(ouch.begin(), ouch.end());
    } else if constexpr (Mode == IoMode::Socket) {
        const auto pkt = soup::packUnsequencedData(m_sock.soupBuf.data(), ouch);
        if (::send(m_sock.oeFd, pkt.data(), pkt.size(), MSG_NOSIGNAL) <= 0) {
            return false;
        }
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

template <IoMode Mode, Strategy Strat, class Io>
std::uint16_t DutSession<Mode, Strat, Io>::udpDstPort(const std::uint8_t* frame) noexcept {
    constexpr std::size_t off = net::kEthHeaderSize + net::kIpv4HeaderSize + 2;
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[off]) << 8) | frame[off + 1]);
}

}
