#pragma once
//
// Session server: SoupBinTCP <-> OUCH <-> matching <-> ITCH <-> MoldUDP64. A compile-time
// IoMode selects in-memory capture (Loopback, for tests), kernel sockets (Socket, live), or a
// hand-framed DPDK transport (Dpdk) supplied through the Tx type parameter.
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

#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/protocol/UdpFramer.hpp"
#include "abt/sim/EngineConfig.hpp"
#include "abt/sim/Venue.hpp"
#include "abt/util/Clock.hpp"

namespace abt {

enum class IoMode { Loopback, Socket, Dpdk };

struct NoTransport {};

template <IoMode Mode, class Tx = NoTransport>
class ExchangeSession {
public:
    explicit ExchangeSession(const ExchangeConfig& cfg = {});
    ~ExchangeSession();
    ExchangeSession(const ExchangeSession&) = delete;
    ExchangeSession& operator=(const ExchangeSession&) = delete;

    void onOrderEntryBytes(std::span<const std::byte> data, std::uint64_t ts);

    OrderId injectSynthetic(Side side, Price tick, Quantity qty, std::uint64_t ts);
    void cancelSynthetic(OrderId ref, std::uint64_t ts);
    void sessionEvent(itch::SystemEventCode code, std::uint64_t ts);

    [[nodiscard]] Price bestBid() const noexcept;
    [[nodiscard]] Price bestAsk() const noexcept;
    [[nodiscard]] const OrderBook& book() const noexcept;

    [[nodiscard]] bool prepareSocketIo(std::uint16_t oePort, const char* mdHost, std::uint16_t mdPort)
        requires (Mode == IoMode::Socket);
    void attachSockets(int oeFd, int mdFd) requires (Mode == IoMode::Socket);
    void prepareDpdk(Tx& tx, const net::Endpoints& mdEp, const net::Endpoints& oeEp)
        requires (Mode == IoMode::Dpdk && TxRing<Tx>);
    template <class TickFn>
    void run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs, TickFn&& onTick)
        requires (Mode == IoMode::Socket);
    void pollOrderEntry(std::uint64_t ts) requires (Mode == IoMode::Dpdk && RxRing<Tx>);
    template <class TickFn>
    void run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs, TickFn&& onTick)
        requires (Mode == IoMode::Dpdk && RxRing<Tx>);

    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedMarketData() const
        requires (Mode == IoMode::Loopback);
    [[nodiscard]] const std::vector<std::vector<std::byte>>& capturedOrderEntry() const
        requires (Mode == IoMode::Loopback);
    void clearCaptured() requires (Mode == IoMode::Loopback);

    struct VenueSink {
        ExchangeSession* s;
        void marketData(std::span<const std::byte> itch) { s->appendMarketData(itch); }
        void orderEntry(std::span<const std::byte> ouch) { s->sendOrderEntry(ouch); }
    };

private:
    struct SocketState { int mdFd = -1; int oeFd = -1; };
    struct Capture {
        std::vector<std::vector<std::byte>> md;
        std::vector<std::vector<std::byte>> oe;
    };
    struct Empty {};
    struct DpdkState {
        Tx*                           tx = nullptr;
        std::optional<net::UdpFramer> mdFramer;
        std::optional<net::UdpFramer> oeFramer;
    };

    void marketDataOut(std::span<const std::byte> b);
    void orderEntryOut(std::span<const std::byte> b);
    void sendDpdk(const net::UdpFramer& fr, std::span<const std::byte> payload);
    void handleSoup(const soup::Packet& p, std::uint64_t ts);
    void dispatchOuch(std::span<const std::byte> payload, std::uint64_t ts);
    template <class Fn> void withMarketData(Fn&& fn);
    void appendMarketData(std::span<const std::byte> itch);
    void flushMarketData();
    void sendOrderEntry(std::span<const std::byte> ouch);

    [[noreturn]] static void die(const char* what);
    static int makeUdpSender(const char* host, std::uint16_t port)
        requires (Mode == IoMode::Socket);
    static int acceptOrderEntry(std::uint16_t port) requires (Mode == IoMode::Socket);

    ExchangeConfig    m_cfg;
    VenueSink         m_sink;
    mold::Packer      m_packer;
    Venue<VenueSink>  m_venue;

    std::vector<std::byte>       m_rxBuf;
    std::array<std::byte, 2048>  m_mdBuf{};
    std::array<std::byte, 512>   m_oeBuf{};
    bool                         m_mdOpen = false;
    std::uint64_t                m_outSeq = 1;

    [[no_unique_address]] std::conditional_t<Mode == IoMode::Socket, SocketState, Empty> m_sock{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Loopback, Capture, Empty> m_cap{};
    [[no_unique_address]] std::conditional_t<Mode == IoMode::Dpdk, DpdkState, Empty> m_dpdk{};
};

template <IoMode Mode, class Tx>
ExchangeSession<Mode, Tx>::ExchangeSession(const ExchangeConfig& cfg)
    : m_cfg(cfg),
      m_sink{this},
      m_packer(cfg.session, 1),
      m_venue(m_sink, cfg.symbol, cfg.stockLocate, cfg.minTick, cfg.maxTick, cfg.wirePerTick) {}

template <IoMode Mode, class Tx>
ExchangeSession<Mode, Tx>::~ExchangeSession() {
    if constexpr (Mode == IoMode::Socket) {
        if (m_sock.oeFd >= 0) ::close(m_sock.oeFd);
        if (m_sock.mdFd >= 0) ::close(m_sock.mdFd);
    }
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::onOrderEntryBytes(std::span<const std::byte> data,
                                                  std::uint64_t ts) {
    m_rxBuf.insert(m_rxBuf.end(), data.begin(), data.end());
    std::size_t off = 0;
    soup::Packet p{};
    for (;;) {
        const std::size_t c = soup::parse({m_rxBuf.data() + off, m_rxBuf.size() - off}, p);
        if (c == 0) break;
        handleSoup(p, ts);
        off += c;
    }
    if (off) m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + static_cast<std::ptrdiff_t>(off));
}

template <IoMode Mode, class Tx>
OrderId ExchangeSession<Mode, Tx>::injectSynthetic(Side side, Price tick, Quantity qty,
                                                   std::uint64_t ts) {
    OrderId ref = 0;
    withMarketData([&] { ref = m_venue.injectSynthetic(side, tick, qty, ts); });
    return ref;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::cancelSynthetic(OrderId ref, std::uint64_t ts) {
    withMarketData([&] { m_venue.cancelSynthetic(ref, ts); });
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
    withMarketData([&] { m_venue.sessionEvent(code, ts); });
}

template <IoMode Mode, class Tx>
Price ExchangeSession<Mode, Tx>::bestBid() const noexcept { return m_venue.bestBid(); }
template <IoMode Mode, class Tx>
Price ExchangeSession<Mode, Tx>::bestAsk() const noexcept { return m_venue.bestAsk(); }
template <IoMode Mode, class Tx>
const OrderBook& ExchangeSession<Mode, Tx>::book() const noexcept { return m_venue.book(); }

template <IoMode Mode, class Tx>
bool ExchangeSession<Mode, Tx>::prepareSocketIo(std::uint16_t oePort, const char* mdHost,
                                                std::uint16_t mdPort)
    requires (Mode == IoMode::Socket) {
    m_sock.mdFd = makeUdpSender(mdHost, mdPort);
    m_sock.oeFd = acceptOrderEntry(oePort);
    return m_sock.oeFd >= 0;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::attachSockets(int oeFd, int mdFd)
    requires (Mode == IoMode::Socket) {
    m_sock.oeFd = oeFd;
    m_sock.mdFd = mdFd;
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::prepareDpdk(Tx& tx, const net::Endpoints& mdEp,
                                            const net::Endpoints& oeEp)
    requires (Mode == IoMode::Dpdk && TxRing<Tx>) {
    m_dpdk.tx = &tx;
    m_dpdk.mdFramer.emplace(mdEp);
    m_dpdk.oeFramer.emplace(oeEp);
}

template <IoMode Mode, class Tx>
template <class TickFn>
void ExchangeSession<Mode, Tx>::run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs,
                                    TickFn&& onTick)
    requires (Mode == IoMode::Socket) {
    std::array<std::byte, 8192> rx{};
    std::uint64_t lastTick = monotonicNs();
    while (stop == 0) {
        pollfd pfd{m_sock.oeFd, POLLIN, 0};
        if (::poll(&pfd, 1, 1) > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::recv(m_sock.oeFd, rx.data(), rx.size(), 0);
            if (n <= 0) { fmt::print(stderr, "exchange-sim: client disconnected\n"); break; }
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
void ExchangeSession<Mode, Tx>::pollOrderEntry(std::uint64_t ts)
    requires (Mode == IoMode::Dpdk && RxRing<Tx>) {
    for (auto f = m_dpdk.tx->tryReceive(); f.status != 0; f = m_dpdk.tx->tryReceive()) {
        const auto raw = f.data;
        if (raw.size() > net::kL2L3L4Overhead) {
            const auto* p = reinterpret_cast<const std::byte*>(raw.data());
            dispatchOuch({p + net::kL2L3L4Overhead, raw.size() - net::kL2L3L4Overhead}, ts);
        }
        m_dpdk.tx->release();
    }
}

template <IoMode Mode, class Tx>
template <class TickFn>
void ExchangeSession<Mode, Tx>::run(volatile std::sig_atomic_t& stop, std::uint64_t tickIntervalNs,
                                    TickFn&& onTick)
    requires (Mode == IoMode::Dpdk && RxRing<Tx>) {
    std::uint64_t lastTick = monotonicNs();
    while (stop == 0) {
        pollOrderEntry(nsSinceMidnightUtc());
        const std::uint64_t now = monotonicNs();
        if (now - lastTick > tickIntervalNs) {
            onTick(nsSinceMidnightUtc());
            lastTick = now;
        }
    }
}

template <IoMode Mode, class Tx>
const std::vector<std::vector<std::byte>>& ExchangeSession<Mode, Tx>::capturedMarketData() const
    requires (Mode == IoMode::Loopback) { return m_cap.md; }
template <IoMode Mode, class Tx>
const std::vector<std::vector<std::byte>>& ExchangeSession<Mode, Tx>::capturedOrderEntry() const
    requires (Mode == IoMode::Loopback) { return m_cap.oe; }
template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::clearCaptured() requires (Mode == IoMode::Loopback) {
    m_cap.md.clear();
    m_cap.oe.clear();
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::sendDpdk(const net::UdpFramer& fr,
                                         std::span<const std::byte> payload) {
    const auto frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + payload.size());
    std::uint8_t* buf = m_dpdk.tx->acquire(frameLen);
    if (buf == nullptr) [[unlikely]] return;
    std::memcpy(buf, fr.header().data(), net::kL2L3L4Overhead);
    std::memcpy(buf + net::kL2L3L4Overhead, payload.data(), payload.size());
    fr.patch(reinterpret_cast<std::byte*>(buf), payload.size());
    m_dpdk.tx->commit();
}

template <IoMode Mode, class Tx>
void ExchangeSession<Mode, Tx>::marketDataOut(std::span<const std::byte> b) {
    if constexpr (Mode == IoMode::Loopback) {
        m_cap.md.emplace_back(b.begin(), b.end());
    } else if constexpr (Mode == IoMode::Socket) {
        (void)::send(m_sock.mdFd, b.data(), b.size(), MSG_NOSIGNAL);
    } else {
        sendDpdk(*m_dpdk.mdFramer, b);
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
void ExchangeSession<Mode, Tx>::dispatchOuch(std::span<const std::byte> payload,
                                             std::uint64_t ts) {
    if (payload.empty()) return;
    const char t = static_cast<char>(payload[0]);
    if (t == static_cast<char>(ouch::InType::EnterOrder) &&
        payload.size() >= sizeof(ouch::EnterOrder)) {
        ouch::EnterOrder o{};
        std::memcpy(&o, payload.data(), sizeof o);
        withMarketData([&] { m_venue.onEnterOrder(o, ts); });
    } else if (t == static_cast<char>(ouch::InType::CancelOrder) &&
               payload.size() >= sizeof(ouch::CancelOrder)) {
        ouch::CancelOrder x{};
        std::memcpy(&x, payload.data(), sizeof x);
        withMarketData([&] { m_venue.onCancelOrder(x, ts); });
    } else if (t == static_cast<char>(ouch::InType::ReplaceOrder) &&
               payload.size() >= sizeof(ouch::ReplaceOrder)) {
        ouch::ReplaceOrder u{};
        std::memcpy(&u, payload.data(), sizeof u);
        withMarketData([&] { m_venue.onReplaceOrder(u, ts); });
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
        m_packer.reset(m_mdBuf.data(), m_mdBuf.size());
        m_mdOpen = true;
    }
    if (!m_packer.append(itch)) {
        flushMarketData();
        m_packer.reset(m_mdBuf.data(), m_mdBuf.size());
        m_mdOpen = true;
        (void)m_packer.append(itch);
    }
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
    if constexpr (Mode == IoMode::Dpdk) {
        sendDpdk(*m_dpdk.oeFramer, ouch);
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
    requires (Mode == IoMode::Socket) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) die("socket(udp)");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) die("inet_pton");
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) die("connect(udp)");
    return fd;
}

template <IoMode Mode, class Tx>
int ExchangeSession<Mode, Tx>::acceptOrderEntry(std::uint16_t port)
    requires (Mode == IoMode::Socket) {
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) die("socket(tcp)");
    int one = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) die("bind");
    if (::listen(lfd, 1) < 0) die("listen");
    fmt::print(stderr, "exchange-sim: waiting for order-entry client on tcp/:{} ...\n", port);
    const int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) {
        if (errno == EINTR) { ::close(lfd); return -1; }
        die("accept");
    }
    int nodelay = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    ::close(lfd);
    fmt::print(stderr, "exchange-sim: order-entry client connected\n");
    return cfd;
}

}
