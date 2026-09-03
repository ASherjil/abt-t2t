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

#include <fmt/format.h>

#include "third_party/abtrda3/RingConcepts.hpp"

#include "t2t/BuildConfig.hpp"
#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/BookTable.hpp"
#include "t2t/dut/ColdShard.hpp"
#include "t2t/dut/DutStatus.hpp"
#include "t2t/dut/LatencyRecorder.hpp"
#include "t2t/dut/OrderManager.hpp"
#include "t2t/dut/SampleContext.hpp"
#include "t2t/dut/SequenceTracker.hpp"
#include "t2t/dut/Strategy.hpp"
#include "t2t/dut/TxSignal.hpp"
#include "t2t/dut/TxStamp.hpp"
#include "t2t/lob/Types.hpp"
#include "t2t/protocol/EthIpUdp.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"
#include "t2t/protocol/UdpFramer.hpp"
#include "t2t/util/Clock.hpp"
#include "t2t/util/HugePageArena.hpp"
#include "t2t/util/Tsc.hpp"
#include "t2t/util/UniqueFd.hpp"

namespace abt::dut {

enum class IoMode {
    Loopback,
    Socket,
    Transport
};

inline constexpr std::size_t kSocketRxRing = 8192;

struct NoTransport {};

struct NoRecorder {
    static void record(std::uint64_t, std::uint64_t = 0, std::uint64_t = 0) noexcept {
    }

    static void setStageNames(const StageNames&) noexcept {
    }
};

struct DutConfig {
    std::vector<std::string>   symbols;
    std::vector<std::uint16_t> locates;
    std::vector<SymbolProfile> profiles;
    Price                      tickWire        = 1;
    std::size_t                coldBandTicks   = 2048;
    std::size_t                hotBandTicks    = 8192;
    double                     bandFraction    = 0.10;
    std::size_t                coldMapSlots    = 1024;
    std::size_t                hotMapSlots     = 1u << 16;
    std::size_t                arenaMb         = 0;
    bool                       coldShard       = false;
    int                        coldCore        = -1;
    std::size_t                coldArenaMb     = 0;
    std::size_t                coldQueue       = 8192;
    bool                       marketHoursOnly = false;
    OrderId                    ownRefMin       = 0;
    std::uint32_t              firstUserRef    = 1;
    std::size_t                queueCapacity   = 1u << 16;
    int                        sigFigs         = 3;
};

template <IoMode Mode, Strategy Strat, class Io = NoTransport>
class DutSession {
public:
    using SwRecorder = std::conditional_t<build::kSwTiming, LatencyRecorder, NoRecorder>;

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
    void run(volatile std::sig_atomic_t& stop, Periodic periodic)
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

    [[nodiscard]] const BookBuilder&     book(std::size_t hot = 0) const noexcept;
    [[nodiscard]] const BookTable&       books() const noexcept;
    [[nodiscard]] std::string            arenaInfo() const;
    [[nodiscard]] const OrderManager&    oms() const noexcept;
    [[nodiscard]] const SequenceTracker& feed() const noexcept;
    [[nodiscard]] LatencyRecorder&       t2t() noexcept;
    [[nodiscard]] SwRecorder&            t2tSw() noexcept
        requires (build::kSwTiming);
    [[nodiscard]] SwRecorder& proc() noexcept
        requires (build::kSwTiming);
    [[nodiscard]] SwRecorder& t2tHol() noexcept
        requires (build::kSwTiming);
    [[nodiscard]] std::uint32_t    ordersSent() const noexcept;
    [[nodiscard]] std::uint64_t    packetsReceived() const noexcept;
    [[nodiscard]] std::uint64_t    foreignMessages() const noexcept;
    [[nodiscard]] std::uint32_t    sessionResets() const noexcept;
    [[nodiscard]] bool             tradingAllowed(std::size_t hot = 0) const noexcept;
    [[nodiscard]] bool             feedValid() const noexcept;
    [[nodiscard]] bool             marketOpen() const noexcept;
    [[nodiscard]] const ColdShard* cold() const noexcept;
    void                           startCold();
    void                           stopCold();
    void                           sendTestOrder() noexcept;
    void                           warmQuotePath() noexcept;
    void                           prefetchQuotePath(std::size_t hot) const noexcept;
    [[nodiscard]] DutStatus        status(std::uint64_t elapsedNs) const noexcept;
    [[nodiscard]] std::uint32_t    feedFaults() const noexcept;
    [[nodiscard]] std::uint64_t    lastFaultSeq() const noexcept;

    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrders() const
        requires (Mode == IoMode::Loopback);

private:
    static constexpr std::uint32_t kInFlight = 1024;

    struct InFlight {
        std::uint32_t userRef = 0;
        std::uint64_t rxHwts  = 0;
        std::uint64_t ctx     = 0;
        bool          live    = false;
    };

    struct Capture {
        std::vector<std::vector<std::byte>> oe;
    };

    struct Empty {};

    struct TransportState {
        static constexpr std::size_t kHeaderKinds = 3;
        using Header                              = std::array<std::byte, net::kL2L3L4Overhead>;
        Io*                              io       = nullptr;
        std::optional<net::UdpFramer>    oeFramer;
        std::array<Header, kHeaderKinds> oeHeaders{};
        std::uint16_t                    ackPort  = 0;
        bool                             loggedIn = false;
    };

    static constexpr std::array<std::size_t, TransportState::kHeaderKinds> kHeaderPayload{
        sizeof(ouch::EnterOrder), sizeof(ouch::ReplaceOrder), sizeof(ouch::CancelOrder)};

    [[nodiscard]] static constexpr std::size_t headerKind(std::size_t payloadLen) noexcept {
        for (std::size_t k = 0; k < TransportState::kHeaderKinds; ++k) {
            if (kHeaderPayload[k] == payloadLen) {
                return k;
            }
        }
        return TransportState::kHeaderKinds;
    }

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
    void invalidateFeed(std::uint64_t seq) noexcept;
    [[nodiscard]] bool sendOrder(std::span<const std::byte> ouch);
    void               recordSend(std::uint32_t userRef, std::uint64_t rxHwts, std::uint64_t ctx) noexcept;
    [[nodiscard]] static std::uint16_t   udpDstPort(const std::uint8_t* frame) noexcept;
    [[nodiscard]] static std::uint64_t   swNow() noexcept;
    [[nodiscard]] static std::uint64_t   swMark() noexcept;
    [[nodiscard]] static SwRecorder      makeSwRecorder(const char* name, const DutConfig& cfg);
    [[nodiscard]] static BookTableConfig tableConfigOf(const DutConfig& cfg, std::pmr::memory_resource* mr,
                                                       BookScope scope);
    void                                 touch(int hot) noexcept;

    DutConfig                m_cfg;
    util::HugePageArena      m_arena;
    BookTable                m_books;
    util::HugePageArena      m_coldArena;
    std::optional<ColdShard> m_cold;
    std::vector<Strat>       m_strats;
    OrderManager             m_oms;
    SequenceTracker          m_seq;
    LatencyRecorder          m_t2t;
    SwRecorder               m_t2tSw;
    SwRecorder               m_proc;
    SwRecorder               m_t2tHol;
    std::uint64_t            m_lastRxTsc    = 0;
    bool                     m_idleSince    = true;
    std::uint32_t            m_ordersSent   = 0;
    std::uint32_t            m_commits      = 0;
    bool                     m_reapHit      = false;
    std::uint64_t            m_packets      = 0;
    std::uint32_t            m_resets       = 0;
    std::uint32_t            m_feedFaults   = 0;
    std::uint64_t            m_lastFault    = 0;
    bool                     m_marketOpen   = false;
    bool                     m_feedValid    = true;
    bool                     m_reconcileAll = false;
    std::uint32_t            m_gen          = 0;
    std::size_t              m_touchedCount = 0;

    std::vector<Outbound>           m_out;
    QuoteTargets                    m_warm{};
    std::vector<std::uint16_t>      m_touched;
    std::vector<std::uint32_t>      m_touchGen;
    std::array<InFlight, kInFlight> m_inflight{};

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty>         m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Socket, SocketState, Empty>       m_sock{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty> m_io{};
};

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::DutSession(const DutConfig& cfg, Strat strat)
    : m_cfg(cfg),
      m_arena(cfg.arenaMb << 20),
      m_books(tableConfigOf(cfg, cfg.arenaMb != 0 ? m_arena.resource() : nullptr,
                            cfg.coldShard ? BookScope::HotOnly : BookScope::All)),
      m_coldArena(cfg.coldShard ? cfg.coldArenaMb << 20 : 0),
      m_strats(m_books.hotCount(), strat),
      m_oms(OmsConfig{.symbols = cfg.symbols, .firstUserRef = cfg.firstUserRef}),
      m_t2t("t2t_hw", cfg.queueCapacity, 1.0, cfg.sigFigs),
      m_t2tSw(makeSwRecorder("t2t_sw", cfg)),
      m_proc(makeSwRecorder("proc", cfg)),
      m_t2tHol(makeSwRecorder("t2t_sw_hol", cfg)),
      m_out(OrderManager::kMaxOutbound * (m_books.hotCount() == 0 ? 1 : m_books.hotCount())),
      m_touched(m_books.hotCount()),
      m_touchGen(m_books.hotCount(), 0) {
    if constexpr (build::kSwTiming) {
        m_t2tSw.setStageNames({"rx", "book", "quote", "tx"});
        m_t2tHol.setStageNames({"rx", "book", "quote", "tx"});
        m_proc.setStageNames({"book", "quote", "tx", "rest"});
    }
    if (cfg.coldShard) {
        m_cold.emplace(tableConfigOf(cfg, cfg.coldArenaMb != 0 ? m_coldArena.resource() : nullptr,
                                     BookScope::ColdOnly),
                       cfg.coldQueue, cfg.coldCore);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
BookTableConfig DutSession<Mode, Strat, Io>::tableConfigOf(const DutConfig&           cfg,
                                                           std::pmr::memory_resource* mr, BookScope scope) {
    BookTableConfig t{};
    t.scope         = scope;
    t.tickWire      = cfg.tickWire;
    t.coldBandTicks = cfg.coldBandTicks;
    t.hotBandTicks  = cfg.hotBandTicks;
    t.bandFraction  = cfg.bandFraction;
    t.coldMapSlots  = cfg.coldMapSlots;
    t.hotMapSlots   = cfg.hotMapSlots;
    t.ownRefMin     = cfg.ownRefMin;
    t.hotSymbols    = cfg.symbols;
    t.profiles      = cfg.profiles;
    t.hotLocates    = cfg.locates;
    t.memory        = mr;
    return t;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::touch(int hot) noexcept {
    const auto h = static_cast<std::size_t>(hot);
    if (m_touchGen[h] != m_gen) {
        m_touchGen[h]               = m_gen;
        m_touched[m_touchedCount++] = static_cast<std::uint16_t>(h);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onMarketData(std::span<const std::byte> moldPacket, std::uint64_t rxHwts,
                                               std::uint64_t rxTsc)
    requires (Mode == IoMode::Loopback || Mode == IoMode::Socket)
{
    applyPacket(moldPacket, rxHwts, rxTsc == 0 ? swNow() : rxTsc);
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
    const int rcvbuf = 64 << 20;
    ::setsockopt(m_sock.mdFd.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
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
void DutSession<Mode, Strat, Io>::run(volatile std::sig_atomic_t& stop, Periodic periodic)
    requires (Mode == IoMode::Socket)
{
    std::vector<std::array<std::byte, 2048>> rxRing(kSocketRxRing);
    std::array<std::byte, 8192>              oeRx{};
    std::size_t                              rxAt = 0;
    while (stop == 0) {
        periodic();
        pollfd pfds[2] = {{m_sock.mdFd.get(), POLLIN, 0}, {m_sock.oeFd.get(), POLLIN, 0}};
        if (::poll(pfds, 2, 1) <= 0) {
            continue;
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            const std::uint64_t rxTsc = swNow();
            auto&               rx    = rxRing[rxAt++ & (kSocketRxRing - 1)];
            const ssize_t       n     = ::recv(m_sock.mdFd.get(), rx.data(), rx.size(), 0);
            if (n > 0) {
                onMarketData({rx.data(), static_cast<std::size_t>(n)}, monotonicNs(), rxTsc);
            }
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.oeFd.get(), oeRx.data(), oeRx.size(), 0);
            if (n > 0) {
                onOrderEntry({oeRx.data(), static_cast<std::size_t>(n)});
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
    for (std::size_t k = 0; k < TransportState::kHeaderKinds; ++k) {
        std::memcpy(m_io.oeHeaders[k].data(), m_io.oeFramer->header().data(), net::kL2L3L4Overhead);
        net::UdpFramer::patch(m_io.oeHeaders[k].data(), kHeaderPayload[k]);
    }
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
    ++m_commits;
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
        const std::uint64_t rxTsc = swNow();
        const auto          f     = m_io.io->tryReceive();
        if (f.status == 0) {
            m_idleSince = true;
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
        m_t2t.record(txHwts - slot.rxHwts, slot.ctx);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
const BookBuilder& DutSession<Mode, Strat, Io>::book(std::size_t hot) const noexcept {
    return m_books.hotBook(hot);
}

template <IoMode Mode, Strategy Strat, class Io>
const BookTable& DutSession<Mode, Strat, Io>::books() const noexcept {
    return m_books;
}

template <IoMode Mode, Strategy Strat, class Io>
std::string DutSession<Mode, Strat, Io>::arenaInfo() const {
    if (m_arena.capacity() == 0) {
        return "heap";
    }
    return fmt::format("{} MB {}", m_arena.capacity() >> 20, m_arena.huge() ? "hugetlb" : "4K-page fallback");
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
DutSession<Mode, Strat, Io>::SwRecorder& DutSession<Mode, Strat, Io>::t2tSw() noexcept
    requires (build::kSwTiming)
{
    return m_t2tSw;
}

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::SwRecorder& DutSession<Mode, Strat, Io>::proc() noexcept
    requires (build::kSwTiming)
{
    return m_proc;
}

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::SwRecorder& DutSession<Mode, Strat, Io>::t2tHol() noexcept
    requires (build::kSwTiming)
{
    return m_t2tHol;
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
    const std::uint64_t           begin = swNow();
    const std::uint64_t           seq   = mold::sequenceOf(moldPacket);
    const std::uint16_t           msgs  = mold::countOf(moldPacket);
    const SequenceTracker::Result r     = m_seq.onPacket(seq, msgs);
    if (r == SequenceTracker::Result::Stale) [[unlikely]] {
        return;
    }
    if (r == SequenceTracker::Result::Gap) [[unlikely]] {
        invalidateFeed(seq);
    }
    if (moldPacket.size() >= mold::kHeaderSize + 2 + 3) [[likely]] {
        const int h = m_books.hotIndexOf(BookTable::locateOf(moldPacket.subspan(mold::kHeaderSize + 2, 3)));
        if (h != BookTable::kCold) {
            prefetchQuotePath(static_cast<std::size_t>(h));
        }
    }
    const std::uint64_t rehashBefore   = m_books.rehashes();
    const std::uint64_t reanchorBefore = m_books.reanchors();
    const std::uint64_t createdBefore  = m_books.created();
    const std::uint64_t rescanBefore   = m_books.rescans();
    m_reapHit                          = false;
    ++m_gen;
    m_touchedCount = 0;
    if (msgs > 1) {
        mold::forEachMessage(moldPacket, [this](std::uint64_t, std::span<const std::byte> msg) {
            m_books.prefetchHotOrders(msg);
        });
    }
    mold::forEachMessage(moldPacket, [this](std::uint64_t, std::span<const std::byte> msg) {
        applyMessage(msg);
    });
    if (m_reconcileAll) [[unlikely]] {
        m_reconcileAll = false;
        for (std::size_t h = 0; h < m_books.hotCount(); ++h) {
            touch(static_cast<int>(h));
        }
    }
    std::uint8_t flags = 0;
    if (m_books.rehashes() != rehashBefore) {
        flags |= SampleContext::kRehash;
    }
    if (m_books.reanchors() != reanchorBefore) {
        flags |= SampleContext::kReanchor;
    }
    if (m_books.created() != createdBefore) {
        flags |= SampleContext::kNewBook;
    }
    if (r == SequenceTracker::Result::Gap) {
        flags |= SampleContext::kGap;
    }
    if (m_books.rescans() != rescanBefore) {
        flags |= SampleContext::kRescan;
    }
    std::size_t         n          = 0;
    bool                sent       = false;
    const std::uint64_t applied    = swMark();
    std::uint64_t       quoteTicks = 0;
    std::uint64_t       txTicks    = 0;
    for (std::size_t k = 0; k < m_touchedCount; ++k) {
        const std::size_t   h       = m_touched[k];
        const std::uint64_t q0      = swMark();
        const QuoteTargets  targets = tradingAllowed(h)
                                          ? m_strats[h].onBook(m_books.hotBook(h), m_oms.account(h))
                                          : QuoteTargets{};
        const std::size_t   base    = n;
        n += m_oms.reconcile(h, targets,
                             std::span<Outbound, OrderManager::kMaxOutbound>{&m_out[base],
                                                                             OrderManager::kMaxOutbound});
        if (n - base >= 2) {
            flags |= SampleContext::kMulti;
        }
        const std::uint64_t q1 = swMark();
        quoteTicks += q1 - q0;
        for (std::size_t i = base; i < n; ++i) {
            const bool ok = sendOrder({m_out[i].buf.data(), m_out[i].len});
            if (ok && !sent) {
                sent = true;
                flags |= SampleContext::kSent | (m_reapHit ? SampleContext::kTxReap : 0);
                const std::uint64_t ctx = SampleContext::pack(seq, msgs, flags);
                if constexpr (build::kSwTiming) {
                    const std::uint64_t now    = tsc::now();
                    const std::uint64_t stages = SampleContext::packStages(begin - rxTsc, applied - begin,
                                                                           q1 - applied, now - q1);
                    m_t2tSw.record(now - rxTsc, ctx, stages);
                    m_t2tHol.record(now - ((m_idleSince || m_lastRxTsc == 0) ? rxTsc : m_lastRxTsc), ctx,
                                    stages);
                }
                recordSend(m_out[i].userRef, rxHwts, ctx);
            }
        }
        txTicks += swMark() - q1;
    }
    if (m_cold) {
        (void)m_cold->push(moldPacket);
    }
    m_lastRxTsc = rxTsc;
    m_idleSince = false;
    if constexpr (build::kSwTiming) {
        if (m_reapHit) {
            flags |= SampleContext::kTxReap;
        }
        const std::uint64_t end = tsc::now();
        m_proc.record(end - begin, SampleContext::pack(seq, msgs, flags),
                      SampleContext::packStages(applied - begin, quoteTicks, txTicks,
                                                end - applied - quoteTicks - txTicks));
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
    if (!m_feedValid) [[unlikely]] {
        return;
    }
    if (m_cold && type != 'R' && !m_books.isHot(BookTable::locateOf(msg))) {
        return;
    }
    const int hot = m_books.apply(msg);
    if (hot != BookTable::kCold) {
        touch(hot);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::onSystemEvent(std::span<const std::byte> msg) noexcept {
    if (msg.size() < sizeof(itch::SystemEvent)) {
        return;
    }
    const auto* s = reinterpret_cast<const itch::SystemEvent*>(msg.data());
    switch (static_cast<itch::SystemEventCode>(s->eventCode)) {
        case itch::SystemEventCode::StartOfMessages:
            m_books.clearAll();
            m_marketOpen   = false;
            m_feedValid    = true;
            m_reconcileAll = true;
            ++m_resets;
            break;
        case itch::SystemEventCode::StartOfMarketHours:
            m_marketOpen   = true;
            m_reconcileAll = true;
            break;
        case itch::SystemEventCode::EndOfMarketHours:
            m_marketOpen   = false;
            m_reconcileAll = true;
            break;
        default:
            break;
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::sendTestOrder() noexcept {
    if constexpr (Mode == IoMode::Transport) {
        Outbound out{};
        m_oms.encodeTestOrder(out);
        (void)sendOrder({out.buf.data(), out.len});
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::prefetchQuotePath(std::size_t hot) const noexcept {
    __builtin_prefetch(&m_strats[hot]);
    m_oms.prefetch(hot);
    __builtin_prefetch(&m_out[OrderManager::kMaxOutbound * hot]);
    if constexpr (Mode == IoMode::Transport) {
        __builtin_prefetch(m_io.oeHeaders.data());
        __builtin_prefetch(m_io.oeHeaders.data() + 1);
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::warmQuotePath() noexcept {
    for (std::size_t h = 0; h < m_books.hotCount(); ++h) {
        prefetchQuotePath(h);
        m_warm = m_strats[h].onBook(m_books.hotBook(h), m_oms.account(h));
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::tradingAllowed(std::size_t hot) const noexcept {
    return m_feedValid && (!m_cfg.marketHoursOnly || (m_marketOpen && m_books.hot(hot).trading));
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::invalidateFeed(std::uint64_t seq) noexcept {
    m_feedValid    = false;
    m_lastFault    = seq;
    m_reconcileAll = true;
    ++m_feedFaults;
    m_books.clearAll();
    if (m_cold) {
        m_cold->reset();
    }
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::feedValid() const noexcept {
    return m_feedValid;
}

template <IoMode Mode, Strategy Strat, class Io>
bool DutSession<Mode, Strat, Io>::marketOpen() const noexcept {
    return m_marketOpen;
}

template <IoMode Mode, Strategy Strat, class Io>
const ColdShard* DutSession<Mode, Strat, Io>::cold() const noexcept {
    return m_cold ? &*m_cold : nullptr;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::startCold() {
    if (m_cold) {
        m_cold->start();
    }
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::stopCold() {
    if (m_cold) {
        m_cold->stop();
    }
}

template <IoMode Mode, Strategy Strat, class Io>
DutStatus DutSession<Mode, Strat, Io>::status(std::uint64_t elapsedNs) const noexcept {
    const OmsStats&    s = m_oms.stats();
    const BookBuilder& b = m_books.hotBook(0);
    return DutStatus{.elapsedNs = elapsedNs,
                     .packets   = m_packets,
                     .seq       = m_seq.expected(),
                     .gaps      = m_seq.gaps(),
                     .live      = m_books.liveOrders(),
                     .enters    = s.enters,
                     .replaces  = s.replaces,
                     .cancels   = s.cancels,
                     .accepts   = s.accepts,
                     .fills     = s.fills,
                     .rejects   = s.rejects,
                     .position  = m_oms.netPosition(),
                     .sent      = m_ordersSent,
                     .bid       = b.bestBid(),
                     .ask       = b.bestAsk(),
                     .feedValid = m_feedValid};
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint32_t DutSession<Mode, Strat, Io>::feedFaults() const noexcept {
    return m_feedFaults;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::lastFaultSeq() const noexcept {
    return m_lastFault;
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::foreignMessages() const noexcept {
    return m_books.undirected();
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
        const std::size_t kind = headerKind(ouch.size());
        if (kind < TransportState::kHeaderKinds) [[likely]] {
            std::memcpy(buf, m_io.oeHeaders[kind].data(), net::kL2L3L4Overhead);
            std::memcpy(buf + net::kL2L3L4Overhead, ouch.data(), ouch.size());
        } else {
            std::memcpy(buf, m_io.oeFramer->header().data(), net::kL2L3L4Overhead);
            std::memcpy(buf + net::kL2L3L4Overhead, ouch.data(), ouch.size());
            m_io.oeFramer->patch(reinterpret_cast<std::byte*>(buf), ouch.size());
        }
        m_io.io->commit();
        if ((++m_commits & (kTxSignalEvery - 1)) == 0) [[unlikely]] {
            m_reapHit = true;
        }
    }
    ++m_ordersSent;
    return true;
}

template <IoMode Mode, Strategy Strat, class Io>
void DutSession<Mode, Strat, Io>::recordSend(std::uint32_t userRef, std::uint64_t rxHwts,
                                             std::uint64_t ctx) noexcept {
    m_inflight[userRef % kInFlight] =
        InFlight{.userRef = userRef, .rxHwts = rxHwts, .ctx = ctx, .live = true};
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint16_t DutSession<Mode, Strat, Io>::udpDstPort(const std::uint8_t* frame) noexcept {
    constexpr std::size_t off = net::kEthHeaderSize + net::kIpv4HeaderSize + 2;
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[off]) << 8) | frame[off + 1]);
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::swNow() noexcept {
    if constexpr (build::kSwTiming) {
        return tsc::now();
    } else {
        return 0;
    }
}

template <IoMode Mode, Strategy Strat, class Io>
std::uint64_t DutSession<Mode, Strat, Io>::swMark() noexcept {
    if constexpr (build::kSwTiming) {
        return tsc::mark();
    } else {
        return 0;
    }
}

template <IoMode Mode, Strategy Strat, class Io>
DutSession<Mode, Strat, Io>::SwRecorder DutSession<Mode, Strat, Io>::makeSwRecorder(const char*      name,
                                                                                    const DutConfig& cfg) {
    if constexpr (build::kSwTiming) {
        return SwRecorder(name, cfg.queueCapacity, tsc::nsPerTick(), cfg.sigFigs);
    } else {
        return SwRecorder{};
    }
}

}   // namespace abt::dut
