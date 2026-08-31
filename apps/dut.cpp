#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>

#include <sys/socket.h>
#include <sys/time.h>

#include <fmt/core.h>

#include "abt/dut/DutAppConfig.hpp"
#include "abt/dut/DutSession.hpp"
#include "abt/dut/LatencyRecorder.hpp"
#include "abt/dut/QuoterStrategy.hpp"
#include "abt/util/Tsc.hpp"

using namespace abt;

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

void installSignals() {
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

using Session = dut::DutSession<dut::IoMode::Socket, dut::QuoterStrategy>;

void printReport(Session& sess) {
    sess.t2t().summary();
    sess.proc().summary();
    const dut::OmsStats& s = sess.oms().stats();
    fmt::print("[oms] orders sent={} enters={} replaces={} cancels={} accepts={} fills={} rejects={} "
               "unknown={} position={}\n",
               sess.ordersSent(), s.enters, s.replaces, s.cancels, s.accepts, s.fills, s.rejects,
               s.unknown, sess.oms().account().position);
    const dut::SequenceTracker& f = sess.feed();
    fmt::print("[feed] next seq={} gaps={} missed={} stale={} live orders={}\n",
               f.expected(), f.gaps(), f.missed(), f.stale(), sess.book().liveOrders());
}

int runSocket(const dut::DutAppConfig& cfg) {
    Session sess(cfg.session, dut::QuoterStrategy(cfg.quoter));
    if (!sess.connectVenue(cfg.socket.oeHost.c_str(), cfg.socket.oePort,
                           cfg.socket.mdBindHost.c_str(), cfg.socket.mdPort)) {
        return 1;
    }
    fmt::print(stderr, "dut: connected to {}:{}, market data on {}:{}\n", cfg.socket.oeHost,
               cfg.socket.oePort, cfg.socket.mdBindHost, cfg.socket.mdPort);

    sess.login(cfg.socket.session, cfg.socket.username);

    dut::RecorderThread consumer({&sess.t2t(), &sess.proc()}, cfg.measure.histogramCore);
    consumer.start();

    sess.run(g_stop);

    consumer.stop();
    fmt::print(stderr, "dut: shut down.\n");
    printReport(sess);
    return 0;
}

}

int main() {
    const dut::DutAppConfig cfg = dut::loadDutConfig(ABT_DUT_CONFIG_PATH);
    installSignals();
    tsc::warmUp();

    if (cfg.transport.mode != "socket") {
        fmt::print(stderr, "dut: transport mode '{}' is not wired in this binary yet; use \"socket\"\n",
                   cfg.transport.mode);
        return 1;
    }
    return runSocket(cfg);
}
