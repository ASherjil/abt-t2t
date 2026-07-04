#pragma once
//
// SocketIo.hpp -- the ExchangeSession I/O boundary backed by kernel sockets.
//
// This is config 1 (the "TCP-faithful" path): market data goes out on a connected UDP
// socket, order-entry replies go out on the accepted TCP connection. It is deliberately
// plain BSD sockets -- the kernel provides TCP/UDP. Onload accelerates these same sockets
// at runtime (LD_PRELOAD) with no code change, which is what makes this the DUT's Onload
// config as well.
//
// Both fds are used write-only here (marketDataOut / orderEntryOut); the run loop owns
// reading inbound order-entry bytes and feeding ExchangeSession::onOrderEntryBytes.
//
// First-cut: blocking sockets, send() return ignored. A production venue would handle
// short writes / EAGAIN backpressure; noted.
//
#include <cstddef>
#include <span>
#include <sys/socket.h>

namespace abt::net {

struct SocketIo {
    int mdFd = -1;   // connected UDP socket (market data)   [or a datagram pair, for tests]
    int oeFd = -1;   // accepted TCP connection (order entry) [or a stream pair, for tests]

    void marketDataOut(std::span<const std::byte> b) {
        (void)::send(mdFd, b.data(), b.size(), MSG_NOSIGNAL);
    }
    void orderEntryOut(std::span<const std::byte> b) {
        (void)::send(oeFd, b.data(), b.size(), MSG_NOSIGNAL);
    }
};

}  // namespace abt::net
