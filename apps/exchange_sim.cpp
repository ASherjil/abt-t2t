//
// exchange_sim.cpp -- the runnable exchange simulator (config 1: kernel sockets).
//
//   order entry : TCP listener; accepts one client (the DUT), speaks SoupBinTCP.
//   market data : connected UDP socket; publishes MoldUDP64/ITCH to the DUT.
//
// A synthetic flow generator keeps a live two-sided market; the poll loop interleaves
// draining inbound order entry with driving the generator. This is plain BSD sockets --
// the kernel provides TCP/UDP; running it under Onload accelerates both with no change.
//
// Usage: exchange_sim [order_entry_port] [md_host] [md_port]   (defaults 5001 127.0.0.1 5002)
//
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <span>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "abt/net/SocketIo.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "abt/sim/FlowGenerator.hpp"
#include "abt/util/Clock.hpp"

using namespace abt;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

[[noreturn]] void die(const char* what) {
    std::perror(what);
    std::exit(1);
}

int makeUdpSender(const char* host, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) die("socket(udp)");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) die("inet_pton");
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) die("connect(udp)");
    return fd;
}

int acceptOrderEntry(std::uint16_t port) {
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
    std::fprintf(stderr, "exchange-sim: waiting for order-entry client on tcp/:%u ...\n", port);
    const int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) die("accept");
    int nodelay = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);  // no Nagle
    ::close(lfd);
    return cfd;
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint16_t oePort = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 5001;
    const char* mdHost         = argc > 2 ? argv[2] : "127.0.0.1";
    const std::uint16_t mdPort = argc > 3 ? static_cast<std::uint16_t>(std::atoi(argv[3])) : 5002;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const int mdFd = makeUdpSender(mdHost, mdPort);
    const int oeFd = acceptOrderEntry(oePort);
    std::fprintf(stderr, "exchange-sim: client connected; publishing market data to udp/%s:%u\n",
                 mdHost, mdPort);

    net::SocketIo io{mdFd, oeFd};
    using Session = sim::ExchangeSession<net::SocketIo>;
    Session ex(io, {});
    sim::FlowGenerator<Session> gen(ex, {});

    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, util::nsSinceMidnightUtc());
    gen.run(200, util::nsSinceMidnightUtc(), 0);   // prime a two-sided book

    std::array<std::byte, 8192> rx{};
    std::uint64_t lastGen = util::monotonicNs();
    while (g_stop == 0) {
        pollfd pfd{oeFd, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 1);          // 1 ms tick
        if (pr > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::recv(oeFd, rx.data(), rx.size(), 0);
            if (n <= 0) { std::fprintf(stderr, "exchange-sim: client disconnected\n"); break; }
            ex.onOrderEntryBytes({rx.data(), static_cast<std::size_t>(n)},
                                 util::nsSinceMidnightUtc());
        }
        const std::uint64_t now = util::monotonicNs();
        if (now - lastGen > 100'000) {              // drive the market ~every 100 us
            gen.step(util::nsSinceMidnightUtc());
            lastGen = now;
        }
    }

    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, util::nsSinceMidnightUtc());
    ::close(oeFd);
    ::close(mdFd);
    std::fprintf(stderr, "exchange-sim: shut down.\n");
    return 0;
}
