#pragma once

#include <csignal>
#include <cstdint>
#include <vector>

#include <fmt/core.h>

#include "t2t/BuildConfig.hpp"
#include "t2t/config/BackendTraits.hpp"
#include "t2t/dut/DutAppConfig.hpp"
#include "t2t/dut/DutSession.hpp"
#include "t2t/dut/LatencyRecorder.hpp"
#include "t2t/dut/QuoterStrategy.hpp"
#include "t2t/util/Affinity.hpp"
#include "t2t/util/Clock.hpp"
#include "t2t/util/MemLock.hpp"

namespace abt::dut {

inline constexpr std::uint64_t kDutLogPeriodNs       = 1'000'000'000ull;
inline constexpr std::uint64_t kDutPollsPerClockRead = 1u << 16;

template <class Session>
void printDutReport(Session& sess, util::ThreadCounters atStart, util::ThreadCounters now) {
    const util::ProcessMemory mem = util::processMemory();
    fmt::print("[mem] rss={} MB peak={} MB hugetlb={} MB\n", mem.rssMb, mem.peakRssMb, mem.hugetlbMb);
    fmt::print("[mem] hot-thread page faults during run: minor={} major={}\n",
               now.minorFaults - atStart.minorFaults, now.majorFaults - atStart.majorFaults);
    fmt::print("[sched] hot-thread context switches during run: involuntary={} voluntary={}\n",
               now.involuntarySwitches - atStart.involuntarySwitches,
               now.voluntarySwitches - atStart.voluntarySwitches);
    sess.t2t().summary();
    if constexpr (build::kSwTiming) {
        sess.t2tSw().summary();
        sess.t2tHol().summary();
        sess.proc().summary();
    }
    const OmsStats& s = sess.oms().stats();
    fmt::print("[oms] orders sent={} enters={} replaces={} cancels={} accepts={} fills={} rejects={} "
               "(replace-not-allowed={} price={} qty={} other={}) unknown={} test={} position={}\n",
               sess.ordersSent(), s.enters, s.replaces, s.cancels, s.accepts, s.fills, s.rejects,
               s.rejReplace, s.rejPrice, s.rejQty, s.rejOther, s.unknown, s.tests, sess.oms().netPosition());
    for (std::size_t h = 0; h < sess.books().hotCount(); ++h) {
        const HotSymbol& hs = sess.books().hot(h);
        fmt::print("[oms] {:<8} locate={} resolved={} position={} bid={} ask={} live={}\n", hs.name,
                   hs.locate, hs.resolved, sess.oms().account(h).position, sess.book(h).bestBid(),
                   sess.book(h).bestAsk(), sess.book(h).liveOrders());
    }
    const SequenceTracker& f = sess.feed();
    fmt::print("[feed] packets={} next seq={} gaps={} missed={} stale={} foreign msgs={} resets={} "
               "live orders={}\n",
               sess.packetsReceived(), f.expected(), f.gaps(), f.missed(), f.stale(), sess.foreignMessages(),
               sess.sessionResets(), sess.books().liveOrders());
    fmt::print("[feed] symbols booked={} profiled={} undirected msgs={} rehashes={} reanchors={} rescans={} "
               "books={} MB arena={}\n",
               sess.books().symbols(), sess.books().profiled(), sess.books().undirected(),
               sess.books().rehashes(), sess.books().reanchors(), sess.books().rescans(),
               sess.books().footprintBytes() >> 20, sess.arenaInfo());
    if (const ColdShard* c = sess.cold(); c != nullptr) {
        const BookTable& cb = c->books();
        fmt::print("[cold] core={} symbols booked={} profiled={} undirected msgs={} rehashes={} reanchors={} "
                   "books={} MB live orders={} ring: packets={} msgs={} dropped={} stale={} max depth={}\n",
                   c->core(), cb.symbols(), cb.profiled(), cb.undirected(), cb.rehashes(), cb.reanchors(),
                   cb.footprintBytes() >> 20, cb.liveOrders(), c->packets(), c->applied(), c->dropped(),
                   c->stale(), c->maxDepth());
    }
    if (sess.feedFaults() > 0) {
        fmt::print("[feed] FAULTS={} last at seq={} state={}: book invalidated and quoting stopped until the "
                   "next StartOfMessages\n",
                   sess.feedFaults(), sess.lastFaultSeq(), sess.feedValid() ? "recovered" : "INVALID");
    }
}

template <class Session>
std::vector<LatencyRecorder*> recordersOf(Session& sess) {
    std::vector<LatencyRecorder*> recs{&sess.t2t()};
    if constexpr (build::kSwTiming) {
        recs.push_back(&sess.t2tSw());
        recs.push_back(&sess.proc());
        recs.push_back(&sess.t2tHol());
    }
    return recs;
}

[[nodiscard]] inline FlushConfig flushOf(const MeasureConfig& m) {
    return FlushConfig{.logFile    = m.logFile,
                       .intervalNs = static_cast<std::uint64_t>(m.flushIntervalS) * 1'000'000'000ull};
}

template <BackendTraits T>
int runDut(const DutAppConfig& cfg, typename T::Type& backend, volatile std::sig_atomic_t& stop) {
    const std::uint64_t start   = monotonicNs();
    std::uint64_t       nextLog = start + kDutLogPeriodNs;

    RecorderThread::StatusQueue statusQ(64);
    if constexpr (kIsSocketBackend<T>) {
        DutSession<IoMode::Socket, QuoterStrategy> sess(cfg.session, QuoterStrategy(cfg.quoter));
        if (!sess.connectVenue(cfg.socket.oeHost.c_str(), cfg.socket.oePort, cfg.socket.mdBindHost.c_str(),
                               cfg.socket.mdPort)) {
            return 1;
        }
        fmt::print(stderr, "dut: connected to {}:{}, market data on {}:{}\n", cfg.socket.oeHost,
                   cfg.socket.oePort, cfg.socket.mdBindHost, cfg.socket.mdPort);
        sess.login(cfg.socket.session, cfg.socket.username);

        RecorderThread consumer(recordersOf(sess), cfg.measure.histogramCore, flushOf(cfg.measure), &statusQ);
        sess.startCold();
        (void)util::pinThread(cfg.transport.cpuCore);
        const util::ThreadCounters countersAtStart = util::threadCounters();
        sess.run(stop, [&] {
            const std::uint64_t now = monotonicNs();
            if (now >= nextLog) {
                (void)statusQ.try_push(sess.status(now - start));
                nextLog += kDutLogPeriodNs;
            }
        });
        const util::ThreadCounters countersAtEnd = util::threadCounters();
        sess.stopCold();
        consumer.stop();
        printDutStatus(sess.status(monotonicNs() - start));
        printDutReport(sess, countersAtStart, countersAtEnd);
        return 0;
    } else {
        DutSession<IoMode::Transport, QuoterStrategy, typename T::Type> sess(cfg.session,
                                                                             QuoterStrategy(cfg.quoter));
        if (!sess.prepareTransport(backend, cfg.transport.orderEntry, T::kMaxTxFrame)) {
            fmt::print(stderr, "dut: backend max TX frame {} B is smaller than an OUCH order frame\n",
                       T::kMaxTxFrame);
            return 1;
        }
        fmt::print(stderr, "dut: {} on {} (core {}), md udp/{} <- {}, oe udp/{} -> {}\n", T::kName,
                   cfg.transport.interface, cfg.transport.cpuCore, cfg.transport.marketData.srcPort,
                   cfg.transport.marketData.dstPort, cfg.transport.orderEntry.srcPort,
                   cfg.transport.orderEntry.dstPort);

        RecorderThread consumer(recordersOf(sess), cfg.measure.histogramCore, flushOf(cfg.measure), &statusQ);
        sess.startCold();
        if (!util::pinThread(cfg.transport.cpuCore)) {
            fmt::print(stderr, "dut: cannot pin to core {}\n", cfg.transport.cpuCore);
            return 1;
        }
        const util::ThreadCounters countersAtStart = util::threadCounters();
        sess.sendLogin(cfg.socket.session, cfg.socket.username);
        std::uint64_t nextLogin = monotonicNs() + kDutLogPeriodNs;
        std::uint64_t polls     = 0;
        while (stop == 0) {
            sess.poll();
            if (++polls == kDutPollsPerClockRead) {
                polls                   = 0;
                const std::uint64_t now = monotonicNs();
                if (!sess.sessionEstablished() && now >= nextLogin) {
                    sess.sendLogin(cfg.socket.session, cfg.socket.username);
                    nextLogin = now + kDutLogPeriodNs;
                }
                if (now >= nextLog) {
                    (void)statusQ.try_push(sess.status(now - start));
                    nextLog += kDutLogPeriodNs;
                    if (sess.sessionEstablished() && !sess.marketOpen()) {
                        sess.warmQuotePath();
                        sess.sendTestOrder();
                    }
                }
            }
        }
        const util::ThreadCounters countersAtEnd = util::threadCounters();
        sess.stopCold();
        consumer.stop();
        printDutStatus(sess.status(monotonicNs() - start));
        printDutReport(sess, countersAtStart, countersAtEnd);
        return 0;
    }
}

[[nodiscard]] inline NicSpec nicOf(const DutAppConfig& cfg) {
    return NicSpec{.interface = cfg.transport.interface,
                   .driver    = cfg.transport.driver,
                   .cpuCore   = cfg.transport.cpuCore};
}

}   // namespace abt::dut
