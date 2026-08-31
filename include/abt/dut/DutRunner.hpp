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

namespace abt::dut {

template <class Session>
void printDutReport(Session& sess) {
    sess.t2t().summary();
    sess.proc().summary();
    const OmsStats& s = sess.oms().stats();
    fmt::print("[oms] orders sent={} enters={} replaces={} cancels={} accepts={} fills={} rejects={} "
               "unknown={} position={}\n",
               sess.ordersSent(), s.enters, s.replaces, s.cancels, s.accepts, s.fills, s.rejects,
               s.unknown, sess.oms().account().position);
    const SequenceTracker& f = sess.feed();
    fmt::print("[feed] next seq={} gaps={} missed={} stale={} live orders={}\n",
               f.expected(), f.gaps(), f.missed(), f.stale(), sess.book().liveOrders());
}

template <BackendTraits T>
int runDut(const DutAppConfig& cfg, typename T::Type& backend, volatile std::sig_atomic_t& stop) {
    if constexpr (kIsSocketBackend<T>) {
        DutSession<IoMode::Socket, QuoterStrategy> sess(cfg.session, QuoterStrategy(cfg.quoter));
        if (!sess.connectVenue(cfg.socket.oeHost.c_str(), cfg.socket.oePort,
                               cfg.socket.mdBindHost.c_str(), cfg.socket.mdPort)) {
            return 1;
        }
        fmt::print(stderr, "dut: connected to {}:{}, market data on {}:{}\n", cfg.socket.oeHost,
                   cfg.socket.oePort, cfg.socket.mdBindHost, cfg.socket.mdPort);
        sess.login(cfg.socket.session, cfg.socket.username);

        RecorderThread consumer({&sess.t2t(), &sess.proc()}, cfg.measure.histogramCore);
        consumer.start();
        sess.run(stop);
        consumer.stop();
        printDutReport(sess);
        return 0;
    } else {
        if (!util::pinThread(cfg.transport.cpuCore)) {
            fmt::print(stderr, "dut: cannot pin to core {}\n", cfg.transport.cpuCore);
            return 1;
        }
        DutSession<IoMode::Transport, QuoterStrategy, typename T::Type> sess(
            cfg.session, QuoterStrategy(cfg.quoter));
        sess.prepareTransport(backend, cfg.transport.orderEntry);
        fmt::print(stderr, "dut: {} on {} (core {}), md udp/{} <- {}, oe udp/{} -> {}\n", T::kName,
                   cfg.transport.interface, cfg.transport.cpuCore,
                   cfg.transport.marketData.srcPort, cfg.transport.marketData.dstPort,
                   cfg.transport.orderEntry.srcPort, cfg.transport.orderEntry.dstPort);

        RecorderThread consumer({&sess.t2t(), &sess.proc()}, cfg.measure.histogramCore);
        consumer.start();
        while (stop == 0) {
            sess.poll();
        }
        consumer.stop();
        printDutReport(sess);
        return 0;
    }
}

[[nodiscard]] inline NicSpec nicOf(const DutAppConfig& cfg) {
    return NicSpec{cfg.transport.interface, cfg.transport.driver, cfg.transport.cpuCore};
}

}
