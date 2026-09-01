#pragma once
//
// Session server: SoupBinTCP <-> OUCH <-> matching <-> ITCH <-> MoldUDP64. A compile-time
// IoMode selects in-memory capture (Loopback, for tests), kernel sockets (Socket, live), or a
// ring transport (Transport = Verbs / DPDK / ef_vi via ABTRDA3) supplied through the Tx type parameter.
//

#include <array>
#include <cerrno>
#include <concepts>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/core.h>

#include "third_party/abtrda3/RingConcepts.hpp"

#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"
#include "t2t/protocol/UdpFramer.hpp"
#include "t2t/sim/EngineConfig.hpp"
#include "t2t/sim/Venue.hpp"
#include "t2t/util/Clock.hpp"

namespace abt {

enum class IoMode {
    Loopback,
    Socket,
    Transport
};

struct NoTransport {};

struct SessionStats {
    std::uint64_t mdPackets = 0;
    std::uint64_t oePackets = 0;
    std::uint64_t enters    = 0;
    std::uint64_t cancels   = 0;
    std::uint64_t replaces  = 0;
    std::uint64_t unknown   = 0;
    std::uint64_t txDropped = 0;
    std::uint64_t forwarded = 0;
    std::uint64_t mirrored  = 0;
    std::uint64_t logins    = 0;
};

template <IoMode Mode, class Tx = NoTransport>
class ExchangeSession {
public:
    explicit ExchangeSession(const ExchangeConfig& cfg = {});
    ~ExchangeSession();
    ExchangeSession(const ExchangeSession&)            = delete;
    ExchangeSession& operator=(const ExchangeSession&) = delete;

    void onOrderEntryBytes(std::span<const std::byte> data, std::uint64_t ts);

    OrderId injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts);
    void    cancelSynthetic(OrderId ref, std::uint64_t ts);
    void    sessionEvent(itch::SystemEventCode code, std::uint64_t ts);

    void                             replayMessage(std::span<const std::byte> msg, std::uint64_t ts);
    void                             flushMarketData();
    void                             resetDay(std::uint64_t ts);
    [[nodiscard]] const MirrorStats& mirrorStats() const noexcept;
    [[nodiscard]] std::size_t        clientOrders() const noexcept;
    [[nodiscard]] bool               clientSeen() const noexcept;

    [[nodiscard]] Price               bestBid() const noexcept;
    [[nodiscard]] Price               bestAsk() const noexcept;
    [[nodiscard]] const OrderBook&    book() const noexcept;
    [[nodiscard]] const SessionStats& stats() const noexcept;
    [[nodiscard]] std::uint64_t       trades() const noexcept;
    [[nodiscard]] std::size_t         liveOrders() const noexcept;

    [[nodiscard]] bool prepareSocketIo(std::uint16_t oePort, const char* mdHost, std::uint16_t mdPort)
        requires (Mode == IoMode::Socket);
    void attachSockets(int oeFd, int mdFd)
        requires (Mode == IoMode::Socket);
    void prepareTransport(Tx& tx, const net::Endpoints& mdEp, const net::Endpoints& oeEp,
                          std::uint32_t maxTxFrame = 0)
        requires (Mode == IoMode::Transport && TxRing<Tx>);
    template <class TickFn>
    void run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs, TickFn&& onTick)
        requires (Mode == IoMode::Socket);
    [[nodiscard]] bool pollOrderEntry(std::uint64_t ts)
        requires (Mode == IoMode::Socket);
    [[nodiscard]] bool pollOrderEntry(std::uint64_t ts)
        requires (Mode == IoMode::Transport && RxRing<Tx>);
    template <class TickFn>
    void run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs, TickFn&& onTick)
        requires (Mode == IoMode::Transport && RxRing<Tx>);

    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedMarketData() const
        requires (Mode == IoMode::Loopback);
    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrderEntry() const
        requires (Mode == IoMode::Loopback);
    void clearCaptured()
        requires (Mode == IoMode::Loopback);

    struct VenueSink {
        ExchangeSession* s;

        void marketData(std::span<const std::byte> itch) {
            s->appendMarketData(itch);
        }

        void orderEntry(std::span<const std::byte> ouch) {
            s->sendOrderEntry(ouch);
        }
    };

private:
    struct SocketState {
        int mdFd = -1;
        int oeFd = -1;
    };

    struct Capture {
        std::vector<std::vector<std::byte>> md;
        std::vector<std::vector<std::byte>> oe;
    };

    struct Empty {};

    struct TransportState {
        Tx*                            tx = nullptr;
        std::optional<net::UdpFramer>  mdFramer;
        std::optional<net::UdpFramer>  oeFramer;
        std::uint32_t                  maxTxFrame = 0;
        std::array<std::uint8_t, 2048> frame{};
    };

    void marketDataOut(std::span<const std::byte> b);
    void orderEntryOut(std::span<const std::byte> b);
    void sendFrame(const net::UdpFramer& fr, std::span<const std::byte> payload);
    void handleSoup(const soup::Packet& p, std::uint64_t ts);
    void dispatchOuch(std::span<const std::byte> payload, std::uint64_t ts);
    template <class Fn>
    void                      withMarketData(Fn&& fn);
    void                      appendMarketData(std::span<const std::byte> itch);
    void                      mirror(std::span<const std::byte> msg, std::uint64_t ts);
    [[nodiscard]] std::size_t mdCapacity() const noexcept;
    void                      sendOrderEntry(std::span<const std::byte> ouch);

    [[noreturn]] static void die(const char* what);
    static int               makeUdpSender(const char* host, std::uint16_t port)
        requires (Mode == IoMode::Socket);
    static int acceptOrderEntry(std::uint16_t port)
        requires (Mode == IoMode::Socket);

    ExchangeConfig   m_cfg;
    VenueSink        m_sink;
    mold::Packer     m_packer;
    Venue<VenueSink> m_venue;
    SessionStats     m_stats{};

    std::vector<std::byte>      m_rxBuf;
    std::array<std::byte, 2048> m_mdBuf{};
    std::array<std::byte, 512>  m_oeBuf{};
    bool                        m_mdOpen = false;
    std::uint64_t               m_outSeq = 1;

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Socket, SocketState, Empty>       m_sock{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty>         m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Transport, TransportState, Empty> m_io{};
};

template <IoMode Mode, class Tx>
ExchangeSession<Mode, Tx>::ExchangeSession(const ExchangeConfig& cfg)
    : m_cfg(cfg),
      m_sink{this},
      m_packer(cfg.session, 1),
      m_venue(m_sink, cfg.symbol, cfg.stockLocate, cfg.minTick, cfg.maxTick, cfg.wirePerTick,
              cfg.firstOrderRef, cfg.liveReserve) {
}

template <IoMode Mode, class Tx>
ExchangeSession<Mode, Tx>::~ExchangeSession() {
    if constexpr (Mode == IoMode::Socket) {
        if (m_sock.oeFd >= 0) {
            ::close(m_sock.oeFd);
        }
        if (m_sock.mdFd >= 0) {
            ::close(m_sock.mdFd);
        }
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::onOrderEntryBytes(std::span<const std::byte> data, std::uint64_t ts) {
    ++m_stats.oePackets;
    m_rxBuf.insert(m_rxBuf.end(), data.begin(), data.end());
    std::size_t  off = 0;
    soup::Packet p{};
    for (;;) {
        const std::size_t c = soup::parse({m_rxBuf.data() + off, m_rxBuf.size() - off}, p);
        if (c == 0) {
            break;
        }
        handleSoup(p, ts);
        off += c;
    }
    if (off) {
        m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + static_cast<std::ptrdiff_t>(off));
    }
}

template <IoMode Mode, class Tx>
OrderId ExchangeSession<Mode, Tx>::injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts) {
    OrderId ref = 0;
    withMarketData([&] {
        ref = m_venue.injectSynthetic(side, tick, qty, ts);
    });
    return ref;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::cancelSynthetic(OrderId ref, std::uint64_t ts) {
    withMarketData([&] {
        m_venue.cancelSynthetic(ref, ts);
    });
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
    withMarketData([&] {
        m_venue.sessionEvent(code, ts);
    });
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::replayMessage(std::span<const std::byte> msg, std::uint64_t ts) {
    if (msg.size() < 11) {
        return;
    }
    ++m_stats.forwarded;
    appendMarketData(msg);
    const auto locate = static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                                   std::to_integer<unsigned>(msg[2]));
    if (locate == m_cfg.stockLocate) {
        mirror(msg, ts);
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::mirror(std::span<const std::byte> msg, std::uint64_t ts) {
    const std::byte* d = msg.data();
    switch (static_cast<char>(msg[0])) {
        case 'A':
        case 'F': {
            if (msg.size() < sizeof(itch::AddOrder)) {
                return;
            }
            const auto* a    = reinterpret_cast<const itch::AddOrder*>(d);
            const Side  side = a->side == static_cast<char>(itch::Side::Buy) ? Side::Buy : Side::Sell;
            m_venue.mirrorAdd(a->orderRef.value(), side, a->price.value(), a->shares.value(), ts);
            break;
        }
        case 'E': {
            if (msg.size() < sizeof(itch::OrderExecuted)) {
                return;
            }
            const auto* e = reinterpret_cast<const itch::OrderExecuted*>(d);
            m_venue.mirrorExecute(e->orderRef.value(), e->executedShares.value(), ts);
            break;
        }
        case 'C': {
            if (msg.size() < sizeof(itch::OrderExecutedWithPrice)) {
                return;
            }
            const auto* c = reinterpret_cast<const itch::OrderExecutedWithPrice*>(d);
            m_venue.mirrorExecute(c->orderRef.value(), c->executedShares.value(), ts);
            break;
        }
        case 'X': {
            if (msg.size() < sizeof(itch::OrderCancel)) {
                return;
            }
            const auto* x = reinterpret_cast<const itch::OrderCancel*>(d);
            m_venue.mirrorCancel(x->orderRef.value(), x->cancelledShares.value(), ts);
            break;
        }
        case 'D': {
            if (msg.size() < sizeof(itch::OrderDelete)) {
                return;
            }
            const auto* x = reinterpret_cast<const itch::OrderDelete*>(d);
            m_venue.mirrorDelete(x->orderRef.value(), ts);
            break;
        }
        case 'U': {
            if (msg.size() < sizeof(itch::OrderReplace)) {
                return;
            }
            const auto* u = reinterpret_cast<const itch::OrderReplace*>(d);
            m_venue.mirrorReplace(u->origOrderRef.value(), u->newOrderRef.value(), u->shares.value(),
                                  u->price.value(), ts);
            break;
        }
        default:
            return;
    }
    ++m_stats.mirrored;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::resetDay(std::uint64_t ts) {
    withMarketData([&] {
        m_venue.resetDay(ts);
    });
}

template <IoMode Mode, class Tx>
const MirrorStats& ExchangeSession<Mode, Tx>::mirrorStats() const noexcept {
    return m_venue.mirrorStats();
}

template <IoMode Mode, class Tx>
std::size_t ExchangeSession<Mode, Tx>::clientOrders() const noexcept {
    return m_venue.clientOrders();
}

template <IoMode Mode, class Tx>
bool ExchangeSession<Mode, Tx>::clientSeen() const noexcept {
    if constexpr (Mode == IoMode::Transport) {
        return m_stats.logins > 0;
    } else {
        return true;
    }
}

template <IoMode Mode, class Tx>
Price ExchangeSession<Mode, Tx>::bestBid() const noexcept {
    return m_venue.bestBid();
}

template <IoMode Mode, class Tx>
Price ExchangeSession<Mode, Tx>::bestAsk() const noexcept {
    return m_venue.bestAsk();
}

template <IoMode Mode, class Tx>
const OrderBook& ExchangeSession<Mode, Tx>::book() const noexcept {
    return m_venue.book();
}

template <IoMode Mode, class Tx>
const SessionStats& ExchangeSession<Mode, Tx>::stats() const noexcept {
    return m_stats;
}

template <IoMode Mode, class Tx>
std::uint64_t ExchangeSession<Mode, Tx>::trades() const noexcept {
    return m_venue.trades();
}

template <IoMode Mode, class Tx>
std::size_t ExchangeSession<Mode, Tx>::liveOrders() const noexcept {
    return m_venue.liveOrders();
}

template <IoMode Mode, class Tx>
bool ExchangeSession<Mode, Tx>::prepareSocketIo(std::uint16_t oePort, const char* mdHost,
                                                std::uint16_t mdPort)
    requires (Mode == IoMode::Socket)
{
    m_sock.mdFd = makeUdpSender(mdHost, mdPort);
    m_sock.oeFd = acceptOrderEntry(oePort);
    return m_sock.oeFd >= 0;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::attachSockets(int oeFd, int mdFd)
    requires (Mode == IoMode::Socket)
{
    m_sock.oeFd = oeFd;
    m_sock.mdFd = mdFd;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::prepareTransport(Tx& tx, const net::Endpoints& mdEp,
                                                 const net::Endpoints& oeEp, std::uint32_t maxTxFrame)
    requires (Mode == IoMode::Transport && TxRing<Tx>)
{
    m_io.tx = &tx;
    m_io.mdFramer.emplace(mdEp);
    m_io.oeFramer.emplace(oeEp);
    m_io.maxTxFrame = maxTxFrame;
}

template <IoMode Mode, class Tx>
template <class TickFn>
void ExchangeSession<Mode, Tx>::run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs,
                                    TickFn&& onTick)
    requires (Mode == IoMode::Socket)
{
    std::array<std::byte, 8192> rx{};
    std::uint64_t               lastTick = monotonicNs();
    while (stop == 0) {
        pollfd pfd{m_sock.oeFd, POLLIN, 0};
        if (::poll(&pfd, 1, 1) > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.oeFd, rx.data(), rx.size(), 0);
            if (n <= 0) {
                fmt::print(stderr, "exchange-sim: client disconnected\n");
                break;
            }
            onOrderEntryBytes({rx.data(), static_cast<std::size_t>(n)}, nsSinceMidnightUtc());
        }
        const std::uint64_t now = monotonicNs();
        if (now - lastTick > tickIntervalNs) {
            onTick(nsSinceMidnightUtc());
            lastTick = now;
        }
    }
}

template <IoMode Mode, class Tx>
bool ExchangeSession<Mode, Tx>::pollOrderEntry(std::uint64_t ts)
    requires (Mode == IoMode::Socket)
{
    std::array<std::byte, 8192> rx{};
    const ssize_t               n = ::recv(m_sock.oeFd, rx.data(), rx.size(), MSG_DONTWAIT);
    if (n > 0) {
        onOrderEntryBytes({rx.data(), static_cast<std::size_t>(n)}, ts);
        return true;
    }
    if (n == 0) {
        fmt::print(stderr, "exchange-sim: client disconnected\n");
        return false;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

template <IoMode Mode, class Tx>
bool ExchangeSession<Mode, Tx>::pollOrderEntry(std::uint64_t ts)
    requires (Mode == IoMode::Transport && RxRing<Tx>)
{
    for (auto f = m_io.tx->tryReceive(); f.status != 0; f = m_io.tx->tryReceive()) {
        ++m_stats.oePackets;
        const auto raw = f.data;
        if (raw.size() > net::kL2L3L4Overhead) {
            const auto*                      p = reinterpret_cast<const std::byte*>(raw.data());
            const std::span<const std::byte> payload{p + net::kL2L3L4Overhead,
                                                     raw.size() - net::kL2L3L4Overhead};
            if (payload[0] == std::byte{0}) [[unlikely]] {
                soup::Packet sp{};
                if (soup::parse(payload, sp) != 0 && sp.type == soup::Type::LoginRequest) {
                    ++m_stats.logins;
                    const auto ack = soup::packLoginAccepted(m_oeBuf.data(), m_cfg.session, m_outSeq);
                    sendFrame(*m_io.oeFramer, ack);
                }
            } else {
                dispatchOuch(payload, ts);
            }
        }
        m_io.tx->release();
    }
    return true;
}

template <IoMode Mode, class Tx>
template <class TickFn>
void ExchangeSession<Mode, Tx>::run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs,
                                    TickFn&& onTick)
    requires (Mode == IoMode::Transport && RxRing<Tx>)
{
    std::uint64_t lastTick = monotonicNs();
    while (stop == 0) {
        (void)pollOrderEntry(nsSinceMidnightUtc());
        const std::uint64_t now = monotonicNs();
        if (now - lastTick > tickIntervalNs) {
            onTick(nsSinceMidnightUtc());
            lastTick = now;
        }
    }
}

template <IoMode Mode, class Tx>
const std::vector<std::vector<std::byte>>& ExchangeSession<Mode, Tx>::capturedMarketData() const
    requires (Mode == IoMode::Loopback)
{
    return m_cap.md;
}

template <IoMode Mode, class Tx>
const std::vector<std::vector<std::byte>>& ExchangeSession<Mode, Tx>::capturedOrderEntry() const
    requires (Mode == IoMode::Loopback)
{
    return m_cap.oe;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::clearCaptured()
    requires (Mode == IoMode::Loopback)
{
    m_cap.md.clear();
    m_cap.oe.clear();
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::sendFrame(const net::UdpFramer& fr, std::span<const std::byte> payload) {
    const std::size_t frameLen = net::kL2L3L4Overhead + payload.size();
    if (frameLen > m_io.frame.size() || (m_io.maxTxFrame != 0 && frameLen > m_io.maxTxFrame)) [[unlikely]] {
        ++m_stats.txDropped;
        return;
    }
    std::uint8_t* buf = m_io.frame.data();
    std::memcpy(buf, fr.header().data(), net::kL2L3L4Overhead);
    std::memcpy(buf + net::kL2L3L4Overhead, payload.data(), payload.size());
    fr.patch(reinterpret_cast<std::byte*>(buf), payload.size());
    if (!m_io.tx->send(std::span<const std::uint8_t>{buf, frameLen})) [[unlikely]] {
        ++m_stats.txDropped;
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::marketDataOut(std::span<const std::byte> b) {
    ++m_stats.mdPackets;
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.md.emplace_back(b.begin(), b.end());
    } else if constexpr (Mode == IoMode::Socket) {
        (void)::send(m_sock.mdFd, b.data(), b.size(), MSG_NOSIGNAL);
    } else {
        sendFrame(*m_io.mdFramer, b);
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::orderEntryOut(std::span<const std::byte> b) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.oe.emplace_back(b.begin(), b.end());
    } else if constexpr (Mode == IoMode::Socket) {
        (void)::send(m_sock.oeFd, b.data(), b.size(), MSG_NOSIGNAL);
    } else {
        (void)b;
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::handleSoup(const soup::Packet& p, std::uint64_t ts) {
    switch (p.type) {
        case soup::Type::LoginRequest: {
            const auto pkt = soup::packLoginAccepted(m_oeBuf.data(), m_cfg.session, m_outSeq);
            orderEntryOut(pkt);
            break;
        }
        case soup::Type::UnsequencedData:
            dispatchOuch(p.payload, ts);
            break;
        case soup::Type::LogoutRequest:
            orderEntryOut(soup::packEndOfSession(m_oeBuf.data()));
            break;
        case soup::Type::ClientHeartbeat:
        default:
            break;
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::dispatchOuch(std::span<const std::byte> payload, std::uint64_t ts) {
    if (payload.empty()) {
        return;
    }
    const char t = static_cast<char>(payload[0]);
    if (t == static_cast<char>(ouch::InType::EnterOrder) && payload.size() >= sizeof(ouch::EnterOrder)) {
        ouch::EnterOrder o{};
        std::memcpy(&o, payload.data(), sizeof o);
        ++m_stats.enters;
        withMarketData([&] {
            m_venue.onEnterOrder(o, ts);
        });
    } else if (t == static_cast<char>(ouch::InType::CancelOrder) &&
               payload.size() >= sizeof(ouch::CancelOrder)) {
        ouch::CancelOrder x{};
        std::memcpy(&x, payload.data(), sizeof x);
        ++m_stats.cancels;
        withMarketData([&] {
            m_venue.onCancelOrder(x, ts);
        });
    } else if (t == static_cast<char>(ouch::InType::ReplaceOrder) &&
               payload.size() >= sizeof(ouch::ReplaceOrder)) {
        ouch::ReplaceOrder u{};
        std::memcpy(&u, payload.data(), sizeof u);
        ++m_stats.replaces;
        withMarketData([&] {
            m_venue.onReplaceOrder(u, ts);
        });
    } else {
        ++m_stats.unknown;
    }
}

template <IoMode Mode, class Tx>
template <class Fn>
void ExchangeSession<Mode, Tx>::withMarketData(Fn&& fn) {
    fn();
    flushMarketData();
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::appendMarketData(std::span<const std::byte> itch) {
    if (!m_mdOpen) {
        m_packer.reset(m_mdBuf.data(), mdCapacity());
        m_mdOpen = true;
    }
    if (!m_packer.append(itch)) {
        flushMarketData();
        m_packer.reset(m_mdBuf.data(), mdCapacity());
        m_mdOpen = true;
        (void)m_packer.append(itch);
    }
}

template <IoMode Mode, class Tx>
std::size_t ExchangeSession<Mode, Tx>::mdCapacity() const noexcept {
    std::size_t cap =
        m_cfg.mdMaxPayload;   // NOLINT(misc-const-correctness) assigned in the if constexpr branch
    if constexpr (Mode == IoMode::Transport) {
        if (m_io.maxTxFrame > net::kL2L3L4Overhead) {
            const std::size_t wire = m_io.maxTxFrame - net::kL2L3L4Overhead;
            if (cap == 0 || wire < cap) {
                cap = wire;
            }
        }
    }
    if (cap < mold::kHeaderSize + 64 || cap > m_mdBuf.size()) {
        return m_mdBuf.size();
    }
    return cap;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::flushMarketData() {
    if (m_mdOpen && m_packer.count() > 0) {
        marketDataOut(m_packer.finalize());
    }
    m_mdOpen = false;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::sendOrderEntry(std::span<const std::byte> ouch) {
    if constexpr (Mode == IoMode::Transport) {
        sendFrame(*m_io.oeFramer, ouch);
    } else {
        const auto pkt = soup::packSequencedData(m_oeBuf.data(), ouch);
        orderEntryOut(pkt);
        ++m_outSeq;
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::die(const char* what) {
    fmt::print(stderr, "{}: {}\n", what, std::strerror(errno));
    std::exit(1);
}

template <IoMode Mode, class Tx>
int ExchangeSession<Mode, Tx>::makeUdpSender(const char* host, std::uint16_t port)
    requires (Mode == IoMode::Socket)
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        die("socket(udp)");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        die("inet_pton");
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        die("connect(udp)");
    }
    return fd;
}

template <IoMode Mode, class Tx>
int ExchangeSession<Mode, Tx>::acceptOrderEntry(std::uint16_t port)
    requires (Mode == IoMode::Socket)
{
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        die("socket(tcp)");
    }
    int one = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        die("bind");
    }
    if (::listen(lfd, 1) < 0) {
        die("listen");
    }
    fmt::print(stderr, "exchange-sim: waiting for order-entry client on tcp/:{} ...\n", port);
    const int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) {
        if (errno == EINTR) {
            ::close(lfd);
            return -1;
        }
        die("accept");
    }
    int nodelay = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    ::close(lfd);
    fmt::print(stderr, "exchange-sim: order-entry client connected\n");
    return cfd;
}

}   // namespace abt
