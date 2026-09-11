#include "t2t/sim/SimStatus.hpp"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <fmt/core.h>

#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/util/Platform.hpp"

namespace abt {

void printSimStatus(const SimStatus& st) {
    const ReplayProgress& p = st.progress;
    const SessionStats&   s = st.stats;
    const MirrorStats&    m = st.mirror;
    fmt::print(stderr,
               "[sim +{:>5}s] loop={} t={} sent={} mir={} late_max={}us late>1ms={} md_pkts={} oe_pkts={} "
               "tx_drop={} "
               "enter={} replace={} cancel={} shadow={}/{} cross={} impact={} self={} unk={} over={} oob={} "
               "rehash={} bid={} ask={} live={} clients={}\n",
               st.elapsedNs / 1'000'000'000ull, p.loop, replay::formatTimeOfDay(p.virtualTs), p.sent,
               s.mirrored, p.maxLateNs / 1000, p.lateOver1ms, s.mdPackets, s.oePackets, s.txDropped, s.enters,
               s.replaces, s.cancels, m.shadowFills, m.shadowShares, m.crossFills, m.impactFills,
               m.selfTrades, m.unknownRef, m.overReduce, m.outOfBand, m.rehashes, st.bid, st.ask, st.live,
               st.clients);
}

SimStatusThread::SimStatusThread(int core, int hotCore)
    : m_thread(loop, &m_queue, core, hotCore) {
}

bool SimStatusThread::push(const SimStatus& st) noexcept {
    return m_queue.try_push(st);
}

void SimStatusThread::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void SimStatusThread::place(int core, int hotCore) noexcept {
    if (core >= 0) {
        (void)util::pinThread(core);
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    for (long c = 0; c < n; ++c) {
        if (c != hotCore) {
            CPU_SET(static_cast<unsigned>(c), &set);
        }
    }
    (void)pthread_setaffinity_np(pthread_self(), sizeof set, &set);
}

void SimStatusThread::drain(Queue& q) {
    while (const SimStatus* st = q.front()) {
        printSimStatus(*st);
        q.pop();
    }
}

void SimStatusThread::loop(const std::stop_token& stop, Queue* q, int core, int hotCore) {
    place(core, hotCore);
    while (!stop.stop_requested()) {
        drain(*q);
        std::this_thread::sleep_for(kSimStatusPoll);
    }
    drain(*q);
}

}   // namespace abt
