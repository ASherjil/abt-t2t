#pragma once

#include <csignal>
#include <cstdint>

#include <fmt/core.h>

#include "abt/config/BackendTraits.hpp"
#include "abt/dut/DutAppConfig.hpp"
#include "abt/dut/DutSession.hpp"
#include "abt/dut/LatencyRecorder.hpp"
#include "abt/dut/QuoterStrategy.hpp"
#include "abt/util/Affinity.hpp"
#include "abt/util/Clock.hpp"

namespace abt::dut {

inline constexpr std::uint64_t kDutLogPeriodNs = 1'000'000'000ull;
inline constexpr std::uint64_t kDutPollsPerClockRead = 1u << 16;

template <class Session>
void logDut(const Session& sess, std::uint64_t elapsedNs) {
    const OmsStats& s = sess.oms().stats();
    const SequenceTracker& f = sess.feed();
    fmt::print(stderr,
               "[dut +{:>4}s] pkts={} seq={} gaps={} sent={} enter={} replace={} cancel={} accept={} "
               "fill={} reject={} pos={} bid={} ask={} live={}\n",
               elapsedNs / 1'000'000'000ull, sess.packetsReceived(), f.expected(), f.gaps(),
               sess.ordersSent(), s.enters, s.replaces, s.cancels, s.accepts, s.fills, s.rejects,
               sess.oms().account().position, sess.book().bestBid(), sess.book().bestAsk(),
               sess.book().liveOrders());
}

template <class Session>
void printDutReport(Session& sess) {
    sess.t2t().summary();
    sess.t2tSw().summary();
    sess.proc().summary();
    const OmsStats& s = sess.oms().stats();
    fmt::print("[oms] orders sent={} enters={} replaces={} cancels={} accepts={} fills={} rejects={} "
               "unknown={} position={}\n",
               sess.ordersSent(), s.enters, s.replaces, s.cancels, s.accepts, s.fills, s.rejects,
               s.unknown, sess.oms().account().position);
    const SequenceTracker& f = sess.feed();
    fmt::print("[feed] packets={} next seq={} gaps={} missed={} stale={} foreign msgs={} resets={} "
               "live orders={}\n",
               sess.packetsReceived(), f.expected(), f.gaps(), f.missed(), f.stale(),
               sess.foreignMessages(), sess.sessionResets(), sess.book().liveOrders());
}

template <BackendTraits T>
int runDut(const DutAppConfig& cfg, typename T::Type& backend, volatile std::sig_atomic_t& stop) {
    const std::uint64_t start = monotonicNs();
    std::uint64_t nextLog = start + kDutLogPeriodNs;

    if constexpr (kIsSocketBackend<T>) {
        (void)util::pinThread(cfg.transport.cpuCore);
        DutSession<IoMode::Socket, QuoterStrategy> sess(cfg.session, QuoterStrategy(cfg.quoter));
        if (!sess.connectVenue(cfg.socket.oeHost.c_str(), cfg.socket.oePort,
                               cfg.socket.mdBindHost.c_str(), cfg.socket.mdPort)) {
            return 1;
        }
        fmt::print(stderr, "dut: connected to {}:{}, market data on {}:{}\n", cfg.socket.oeHost,
                   cfg.socket.oePort, cfg.socket.mdBindHost, cfg.socket.mdPort);
        sess.login(cfg.socket.session, cfg.socket.username);

        RecorderThread consumer({&sess.t2t(), &sess.t2tSw(), &sess.proc()}, cfg.measure.histogramCore);
        consumer.start();
        sess.run(stop, [&] {
            const std::uint64_t now = monotonicNs();
            if (now >= nextLog) {
                logDut(sess, now - start);
                nextLog += kDutLogPeriodNs;
            }
        });
        consumer.stop();
        logDut(sess, monotonicNs() - start);
        printDutReport(sess);
        return 0;
    } else {
        if (!util::pinThread(cfg.transport.cpuCore)) {
            fmt::print(stderr, "dut: cannot pin to core {}\n", cfg.transport.cpuCore);
            return 1;
        }
        DutSession<IoMode::Transport, QuoterStrategy, typename T::Type> sess(
            cfg.session, QuoterStrategy(cfg.quoter));
        if (!sess.prepareTransport(backend, cfg.transport.orderEntry, T::kMaxTxFrame)) {
            fmt::print(stderr, "dut: backend max TX frame {} B is smaller than an OUCH order frame\n",
                       T::kMaxTxFrame);
            return 1;
        }
        fmt::print(stderr, "dut: {} on {} (core {}), md udp/{} <- {}, oe udp/{} -> {}\n", T::kName,
                   cfg.transport.interface, cfg.transport.cpuCore,
                   cfg.transport.marketData.srcPort, cfg.transport.marketData.dstPort,
                   cfg.transport.orderEntry.srcPort, cfg.transport.orderEntry.dstPort);

        RecorderThread consumer({&sess.t2t(), &sess.t2tSw(), &sess.proc()}, cfg.measure.histogramCore);
        consumer.start();
        std::uint64_t polls = 0;
        while (stop == 0) {
            sess.poll();
            if (++polls == kDutPollsPerClockRead) {
                polls = 0;
                const std::uint64_t now = monotonicNs();
                if (now >= nextLog) {
                    logDut(sess, now - start);
                    nextLog += kDutLogPeriodNs;
                }
            }
        }
        consumer.stop();
        logDut(sess, monotonicNs() - start);
        printDutReport(sess);
        return 0;
    }
}

[[nodiscard]] inline NicSpec nicOf(const DutAppConfig& cfg) {
    return NicSpec{cfg.transport.interface, cfg.transport.driver, cfg.transport.cpuCore};
}

}
