//
// Runnable exchange simulator (config 1): TCP order entry + UDP market data via kernel sockets.
//

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/core.h>

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
    fmt::print(stderr, "{}: {}\n", what, std::strerror(errno));
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
    fmt::print(stderr, "exchange-sim: waiting for order-entry client on tcp/:{} ...\n", port);
    const int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) die("accept");
    int nodelay = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    ::close(lfd);
    return cfd;
}

}

int main(int argc, char** argv) {
    const std::uint16_t oePort = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 5001;
    const char* mdHost         = argc > 2 ? argv[2] : "127.0.0.1";
    const std::uint16_t mdPort = argc > 3 ? static_cast<std::uint16_t>(std::atoi(argv[3])) : 5002;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const int mdFd = makeUdpSender(mdHost, mdPort);
    const int oeFd = acceptOrderEntry(oePort);
    fmt::print(stderr, "exchange-sim: client connected; publishing market data to udp/{}:{}\n",
               mdHost, mdPort);

    net::SocketIo io{mdFd, oeFd};
    using Session = sim::ExchangeSession<net::SocketIo>;
    Session ex(io, {});
    sim::FlowGenerator<Session> gen(ex, {});

    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, util::nsSinceMidnightUtc());
    gen.run(200, util::nsSinceMidnightUtc(), 0);

    std::array<std::byte, 8192> rx{};
    std::uint64_t lastGen = util::monotonicNs();
    while (g_stop == 0) {
        pollfd pfd{oeFd, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 1);
        if (pr > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::recv(oeFd, rx.data(), rx.size(), 0);
            if (n <= 0) { fmt::print(stderr, "exchange-sim: client disconnected\n"); break; }
            ex.onOrderEntryBytes({rx.data(), static_cast<std::size_t>(n)},
                                 util::nsSinceMidnightUtc());
        }
        const std::uint64_t now = util::monotonicNs();
        if (now - lastGen > 100'000) {
            gen.step(util::nsSinceMidnightUtc());
            lastGen = now;
        }
    }

    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, util::nsSinceMidnightUtc());
    ::close(oeFd);
    ::close(mdFd);
    fmt::print(stderr, "exchange-sim: shut down.\n");
    return 0;
}
