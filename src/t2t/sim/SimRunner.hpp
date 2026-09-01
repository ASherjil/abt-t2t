#pragma once

#include <csignal>
#include <cstdint>

#include <fmt/core.h>

#include "t2t/config/BackendTraits.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/sim/ExchangeSession.hpp"
#include "t2t/sim/FlowGenerator.hpp"
#include "t2t/sim/SimConfig.hpp"
#include "t2t/util/Affinity.hpp"
#include "t2t/util/Clock.hpp"

namespace abt {

inline constexpr std::uint64_t kSimLogPeriodNs = 1'000'000'000ull;

template <class Session>
void logSim(const Session& ex, std::uint64_t elapsedNs) {
    const SessionStats& s = ex.stats();
    fmt::print(
        stderr,
        "[sim +{:>4}s] md_pkts={} oe_pkts={} tx_drop={} enter={} replace={} cancel={} unknown={} trades={} "
        "bid={} ask={} live={}\n",
        elapsedNs / 1'000'000'000ull, s.mdPackets, s.oePackets, s.txDropped, s.enters, s.replaces, s.cancels,
        s.unknown, ex.trades(), ex.bestBid(), ex.bestAsk(), ex.liveOrders());
}

template <class Session>
void logReplay(const Session& ex, const ReplayProgress& p, std::uint64_t elapsedNs) {
    const SessionStats& s = ex.stats();
    const MirrorStats&  m = ex.mirrorStats();
    fmt::print(stderr,
               "[sim +{:>5}s] loop={} t={} sent={} mir={} late_max={}us late>1ms={} md_pkts={} oe_pkts={} "
               "tx_drop={} "
               "enter={} replace={} cancel={} shadow={}/{} cross={} impact={} self={} unk={} over={} oob={} "
               "bid={} ask={} live={} clients={}\n",
               elapsedNs / 1'000'000'000ull, p.loop, replay::formatTimeOfDay(p.virtualTs), p.sent, s.mirrored,
               p.maxLateNs / 1000, p.lateOver1ms, s.mdPackets, s.oePackets, s.txDropped, s.enters, s.replaces,
               s.cancels, m.shadowFills, m.shadowShares, m.crossFills, m.impactFills, m.selfTrades,
               m.unknownRef, m.overReduce, m.outOfBand, ex.bestBid(), ex.bestAsk(), ex.liveOrders(),
               ex.clientOrders());
}

template <class Session>
int runReplay(Session& ex, const SimConfig& cfg, volatile std::sig_atomic_t& stop) {
    MarketReplay<Session> rp(ex, cfg.replay);
    if (!rp.open()) {
        fmt::print(stderr, "exchange-sim: cannot open replay file '{}'\n", cfg.replay.file);
        return 1;
    }
    fmt::print(stderr, "exchange-sim: replay {} ({}, {} msgs) speed={} loops={} skip_to={} stop_at={}\n",
               cfg.replay.file, rp.progress().preloaded ? "preloaded" : "streamed", rp.fileMessages(),
               cfg.replay.speed, cfg.replay.loops, replay::formatTimeOfDay(cfg.replay.skipToNs),
               replay::formatTimeOfDay(cfg.replay.stopAtNs));
    if (cfg.replay.waitForDut && !ex.clientSeen()) {
        fmt::print(stderr, "exchange-sim: waiting for the DUT to log in on order entry ...\n");
        while (stop == 0 && !ex.clientSeen()) {
            if (!ex.pollOrderEntry(0)) {
                return 0;
            }
        }
        fmt::print(stderr, "exchange-sim: DUT session logged in, starting replay\n");
    }
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
            logReplay(ex, rp.progress(), now - start);
            nextLog += kSimLogPeriodNs;
        }
    }
    ex.flushMarketData();
    logReplay(ex, rp.progress(), monotonicNs() - start);
    return 0;
}

template <class Session>
int runVenue(Session& ex, const SimConfig& cfg, volatile std::sig_atomic_t& stop) {
    if (cfg.replay.enabled) {
        return runReplay(ex, cfg, stop);
    }
    FlowGenerator<Session> gen(ex, cfg.flow);
    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, nsSinceMidnightUtc());
    gen.run(cfg.warmupSteps, nsSinceMidnightUtc(), 0);
    const std::uint64_t start   = monotonicNs();
    std::uint64_t       nextLog = start + kSimLogPeriodNs;
    ex.run(stop, cfg.tickIntervalNs, [&](std::uint64_t ts) {
        gen.step(ts);
        const std::uint64_t now = monotonicNs();
        if (now >= nextLog) {
            logSim(ex, now - start);
            nextLog += kSimLogPeriodNs;
        }
    });
    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, nsSinceMidnightUtc());
    logSim(ex, monotonicNs() - start);
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
        return runVenue(ex, cfg, stop);
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
        return runVenue(ex, cfg, stop);
    }
}

[[nodiscard]] inline NicSpec nicOf(const SimConfig& cfg) {
    return NicSpec{.interface = cfg.transport.interface,
                   .driver    = cfg.transport.driver,
                   .cpuCore   = cfg.transport.cpuCore};
}

}   // namespace abt
