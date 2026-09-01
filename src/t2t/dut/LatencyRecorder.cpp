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
    : m_recorders(std::move(recorders)),
      m_core(cpuCore) {
}

RecorderThread::~RecorderThread() {
    stop();
}

void RecorderThread::start() {
    if (m_run.exchange(true)) {
        return;
    }
    m_thread = std::thread([this] {
        loop();
    });
}

void RecorderThread::stop() {
    if (!m_run.exchange(false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    for (LatencyRecorder* r : m_recorders) {
        (void)r->drain();
    }
}

bool RecorderThread::running() const noexcept {
    return m_run.load(std::memory_order_relaxed);
}

void RecorderThread::loop() noexcept {
    (void)util::pinThread(m_core);
    while (m_run.load(std::memory_order_relaxed)) {
        bool any = false;
        for (LatencyRecorder* r : m_recorders) {
            any |= r->drainOne();
        }
        if (!any) {
            relax();
        }
    }
}

}   // namespace abt::dut
