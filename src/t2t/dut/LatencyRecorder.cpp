#include "t2t/dut/LatencyRecorder.hpp"

#include <utility>

#include <fmt/core.h>

#include "t2t/util/Affinity.hpp"

#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace abt::dut {

namespace {

constexpr std::int64_t kLowestNs  = 1;
constexpr std::int64_t kHighestNs = 10'000'000'000ll;

void relax() noexcept {
#if defined(__x86_64__)
    _mm_pause();
#endif
}

}   // namespace

LatencyRecorder::LatencyRecorder(std::string name, std::size_t queueCapacity, double nsPerUnit, int sigFigs)
    : m_name(std::move(name)),
      m_queue(queueCapacity < 2 ? 2 : queueCapacity),
      m_hist(kLowestNs, kHighestNs, sigFigs),
      m_nsPerUnit(nsPerUnit) {
}

bool LatencyRecorder::drainOne() noexcept {
    const std::uint64_t* raw = m_queue.front();
    if (raw == nullptr) {
        return false;
    }
    const double ns = static_cast<double>(*raw) * m_nsPerUnit;
    m_queue.pop();
    m_hist.record(static_cast<std::int64_t>(ns));
    return true;
}

std::size_t LatencyRecorder::drain() noexcept {
    std::size_t n = 0;
    while (drainOne()) {
        ++n;
    }
    return n;
}

std::string_view LatencyRecorder::name() const noexcept {
    return m_name;
}

const util::Histogram& LatencyRecorder::histogram() const noexcept {
    return m_hist;
}

std::int64_t LatencyRecorder::count() const noexcept {
    return m_hist.count();
}

std::int64_t LatencyRecorder::min() const noexcept {
    return m_hist.min();
}

std::int64_t LatencyRecorder::max() const noexcept {
    return m_hist.max();
}

std::int64_t LatencyRecorder::percentile(double p) const noexcept {
    return m_hist.percentile(p);
}

std::uint64_t LatencyRecorder::dropped() const noexcept {
    return m_dropped.load(std::memory_order_relaxed);
}

void LatencyRecorder::reset() noexcept {
    m_hist.reset();
    m_dropped.store(0, std::memory_order_relaxed);
}

void LatencyRecorder::summary() const {
    fmt::print("[{}] ns: n={} dropped={} min={} p50={} p90={} p99={} p99.9={} p99.99={} max={}\n", m_name,
               count(), dropped(), min(), percentile(50.0), percentile(90.0), percentile(99.0),
               percentile(99.9), percentile(99.99), max());
}

RecorderThread::RecorderThread(std::vector<LatencyRecorder*> recorders, int cpuCore)
    : m_thread([recs = std::move(recorders), cpuCore](const std::stop_token& stop) {
          loop(stop, recs, cpuCore);
      }) {
}

void RecorderThread::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

bool RecorderThread::running() const noexcept {
    return m_thread.joinable();
}

void RecorderThread::loop(const std::stop_token& stop, const std::vector<LatencyRecorder*>& recorders,
                          int core) noexcept {
    (void)util::pinThread(core);
    while (!stop.stop_requested()) {
        bool any = false;
        for (LatencyRecorder* r : recorders) {
            any |= r->drainOne();
        }
        if (!any) {
            relax();
        }
    }
    for (LatencyRecorder* r : recorders) {
        (void)r->drain();
    }
}

}   // namespace abt::dut
