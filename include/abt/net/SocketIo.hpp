#pragma once
//
// ExchangeSession I/O over kernel sockets: TCP order entry + connected UDP market data.
//

#include <cstddef>
#include <span>
#include <sys/socket.h>

namespace abt::net {

struct SocketIo {
    int mdFd = -1;
    int oeFd = -1;

    void marketDataOut(std::span<const std::byte> b) {
        (void)::send(mdFd, b.data(), b.size(), MSG_NOSIGNAL);
    }
    void orderEntryOut(std::span<const std::byte> b) {
        (void)::send(oeFd, b.data(), b.size(), MSG_NOSIGNAL);
    }
};

}
