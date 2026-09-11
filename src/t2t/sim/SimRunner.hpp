#pragma once

#include <csignal>
#include <cstdint>

#include <fmt/core.h>

#include "t2t/config/Config.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/sim/ExchangeSession.hpp"
#include "t2t/sim/SimConfig.hpp"
#include "t2t/sim/SimStatus.hpp"
#include "t2t/util/Memory.hpp"
#include "t2t/util/Platform.hpp"

namespace abt {

inline constexpr std::uint64_t kSimLogPeriodNs = 1'000'000'000ull;

template <class Session>
int runReplay(Session& ex, const SimConfig& cfg, volatile std::sig_atomic_t& stop) {
    MarketReplay<Session> rp(ex, cfg.replay);
    if (!rp.open()) {
        fmt::print(stderr, "exchange-sim: cannot open replay file '{}'\n", cfg.replay.file);
        return 1;
    }
    fmt::print(stderr, "exchange-sim: replay {} ({}, {} msgs) speed={} loops={} skip_to={} stop_at={}\n",
               cfg.replay.file,
               rp.progress().mapped ? "mmap" : (rp.progress().preloaded ? "preloaded" : "streamed"),
               rp.fileMessages(), cfg.replay.speed, cfg.replay.loops,
               replay::formatTimeOfDay(cfg.replay.skipToNs), replay::formatTimeOfDay(cfg.replay.stopAtNs));
    if (cfg.replay.waitForDut && !ex.clientSeen()) {
        fmt::print(stderr, "exchange-sim: waiting for the DUT to log in on order entry ...\n");
        while (stop == 0 && !ex.clientSeen()) {
            if (!ex.pollOrderEntry(0)) {
                return 0;
            }
        }
        fmt::print(stderr,
                   "exchange-sim: DUT session logged in, starting replay (status lines from core {})\n",
                   cfg.transport.logCore);
    }
    SimStatusThread     status(cfg.transport.logCore, cfg.transport.cpuCore);
    const std::uint64_t start   = monotonicNs();
    std::uint64_t       nextLog = start + kSimLogPeriodNs;
    while (stop == 0) {
        if (!ex.pollOrderEntry(rp.progress().virtualTs)) {
            break;
        }
        const std::uint64_t now = monotonicNs();
        if (!rp.pump(now)) {
            break;
        }
        if (now >= nextLog) {
            (void)status.push(simStatus(ex, rp.progress(), now - start));
            nextLog += kSimLogPeriodNs;
        }
    }
    ex.flushMarketData();
    status.stop();
    printSimStatus(simStatus(ex, rp.progress(), monotonicNs() - start));
    const util::ProcessMemory mem = util::processMemory();
    fmt::print(stderr, "[sim mem] rss={} MB peak={} MB hugetlb={} MB (mapped replay file counts in rss)\n",
               mem.rssMb, mem.peakRssMb, mem.hugetlbMb);
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
        fmt::print(stderr, "exchange-sim: publishing market data to udp/{}:{}\n", cfg.socket.mdHost,
                   cfg.socket.mdPort);
        return runReplay(ex, cfg, stop);
    } else {
        if (!util::pinThread(cfg.transport.cpuCore)) {
            fmt::print(stderr, "exchange-sim: cannot pin to core {}\n", cfg.transport.cpuCore);
            return 1;
        }
        ExchangeSession<IoMode::Transport, typename T::Type> ex{cfg.venue};
        ex.prepareTransport(backend, cfg.transport.marketData, cfg.transport.orderEntry, T::kMaxTxFrame);
        fmt::print(stderr, "exchange-sim: {} on {} (core {}), md udp/{} -> {}, oe udp/{} -> {}\n", T::kName,
                   cfg.transport.interface, cfg.transport.cpuCore, cfg.transport.marketData.srcPort,
                   cfg.transport.marketData.dstPort, cfg.transport.orderEntry.srcPort,
                   cfg.transport.orderEntry.dstPort);
        return runReplay(ex, cfg, stop);
    }
}

[[nodiscard]] inline NicSpec nicOf(const SimConfig& cfg) {
    return NicSpec{.interface = cfg.transport.interface,
                   .driver    = cfg.transport.driver,
                   .cpuCore   = cfg.transport.cpuCore};
}

}   // namespace abt
