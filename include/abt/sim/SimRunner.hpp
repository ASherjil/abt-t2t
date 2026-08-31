#pragma once

#include <csignal>
#include <cstdint>

#include <fmt/core.h>

#include "abt/config/BackendTraits.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "abt/sim/FlowGenerator.hpp"
#include "abt/sim/SimConfig.hpp"
#include "abt/util/Affinity.hpp"
#include "abt/util/Clock.hpp"

namespace abt {

template <class Session>
int runVenue(Session& ex, const SimConfig& cfg, volatile std::sig_atomic_t& stop) {
    FlowGenerator<Session> gen(ex, cfg.flow);
    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, nsSinceMidnightUtc());
    gen.run(cfg.warmupSteps, nsSinceMidnightUtc(), 0);
    ex.run(stop, cfg.tickIntervalNs, [&](std::uint64_t ts) { gen.step(ts); });
    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, nsSinceMidnightUtc());
    return 0;
}

template <BackendTraits T>
int runSim(const SimConfig& cfg, typename T::Type& backend, volatile std::sig_atomic_t& stop) {
    if constexpr (kIsSocketBackend<T>) {
        ExchangeSession<IoMode::Socket> ex{cfg.venue};
        if (!ex.prepareSocketIo(cfg.socket.oePort, cfg.socket.mdHost.c_str(), cfg.socket.mdPort)) {
            fmt::print(stderr, "exchange-sim: interrupted before a client connected.\n");
            return 0;
        }
        fmt::print(stderr, "exchange-sim: publishing market data to udp/{}:{}\n",
                   cfg.socket.mdHost, cfg.socket.mdPort);
        return runVenue(ex, cfg, stop);
    } else {
        if (!util::pinThread(cfg.transport.cpuCore)) {
            fmt::print(stderr, "exchange-sim: cannot pin to core {}\n", cfg.transport.cpuCore);
            return 1;
        }
        ExchangeSession<IoMode::Transport, typename T::Type> ex{cfg.venue};
        ex.prepareTransport(backend, cfg.transport.marketData, cfg.transport.orderEntry);
        fmt::print(stderr, "exchange-sim: {} on {} (core {}), md udp/{} -> {}, oe udp/{} -> {}\n",
                   T::kName, cfg.transport.interface, cfg.transport.cpuCore,
                   cfg.transport.marketData.srcPort, cfg.transport.marketData.dstPort,
                   cfg.transport.orderEntry.srcPort, cfg.transport.orderEntry.dstPort);
        return runVenue(ex, cfg, stop);
    }
}

[[nodiscard]] inline NicSpec nicOf(const SimConfig& cfg) {
    return NicSpec{cfg.transport.interface, cfg.transport.driver, cfg.transport.cpuCore};
}

}
