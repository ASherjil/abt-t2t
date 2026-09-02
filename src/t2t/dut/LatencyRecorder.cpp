#include "t2t/dut/LatencyRecorder.hpp"

#include <algorithm>
#include <ctime>
#include <utility>

#include <fmt/format.h>

#include "t2t/dut/SampleContext.hpp"
#include "t2t/util/Affinity.hpp"
#include "t2t/util/Clock.hpp"

#ifdef __x86_64__
#include <immintrin.h>
#endif

namespace abt::dut {

namespace {

constexpr std::int64_t  kLowestNs           = 1;
constexpr std::int64_t  kHighestNs          = 10'000'000'000ll;
constexpr std::uint32_t kDrainsPerClockRead = 4096;

double epochSeconds() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

std::string describe(std::uint64_t ctx) {
    const std::uint8_t f = SampleContext::flags(ctx);
    return fmt::format("seq={} msgs={} {}{}{}{}{}", SampleContext::seq(ctx), SampleContext::msgs(ctx),
                       (f & SampleContext::kSent) != 0 ? "S" : "-",
                       (f & SampleContext::kRehash) != 0 ? "R" : "-",
                       (f & SampleContext::kGap) != 0 ? "G" : "-",
                       (f & SampleContext::kReanchor) != 0 ? "A" : "-",
                       (f & SampleContext::kNewBook) != 0 ? "N" : "-");
}

std::string describeWorst(std::span<const Outlier> worst) {
    std::string out;
    for (const Outlier& o : worst) {
        if (!out.empty()) {
            out += " | ";
        }
        out += fmt::format("{}ns {}", o.ns, describe(o.ctx));
    }
    return out;
}

void relax() noexcept {
#ifdef __x86_64__
    _mm_pause();
#endif
}

}   // namespace

LatencyRecorder::LatencyRecorder(std::string name, std::size_t queueCapacity, double nsPerUnit, int sigFigs)
    : m_name(std::move(name)),
      m_queue(queueCapacity < 2 ? 2 : queueCapacity),
      m_hist(kLowestNs, kHighestNs, sigFigs),
      m_interval(kLowestNs, kHighestNs, sigFigs),
      m_nsPerUnit(nsPerUnit) {
}

bool LatencyRecorder::drainOne() noexcept {
    const Sample* s = m_queue.front();
    if (s == nullptr) {
        return false;
    }
    const Sample sample = *s;
    m_queue.pop();
    const auto v = static_cast<std::int64_t>(static_cast<double>(sample.raw) * m_nsPerUnit);
    m_hist.record(v);
    m_interval.record(v);
    m_worstRun.offer(v, sample.ctx);
    m_worstInterval.offer(v, sample.ctx);
    return true;
}

void LatencyRecorder::Worst::offer(std::int64_t ns, std::uint64_t ctx) noexcept {
    if (n < kWorst) {
        items[n++] = Outlier{ns, ctx};
        return;
    }
    std::size_t lowest = 0;
    for (std::size_t i = 1; i < kWorst; ++i) {
        if (items[i].ns < items[lowest].ns) {
            lowest = i;
        }
    }
    if (ns > items[lowest].ns) {
        items[lowest] = Outlier{ns, ctx};
    }
}

void LatencyRecorder::Worst::clear() noexcept {
    n = 0;
}

std::span<const Outlier> LatencyRecorder::Worst::sorted() noexcept {
    std::sort(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(n),
              [](const Outlier& a, const Outlier& b) {
                  return a.ns > b.ns;
              });
    return {items.data(), n};
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

const util::Histogram& LatencyRecorder::interval() const noexcept {
    return m_interval;
}

std::span<const Outlier> LatencyRecorder::worstRun() noexcept {
    return m_worstRun.sorted();
}

std::span<const Outlier> LatencyRecorder::worstInterval() noexcept {
    return m_worstInterval.sorted();
}

void LatencyRecorder::resetInterval() noexcept {
    m_interval.reset();
    m_worstInterval.clear();
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
    m_interval.reset();
    m_worstRun.clear();
    m_worstInterval.clear();
    m_dropped.store(0, std::memory_order_relaxed);
}

void LatencyRecorder::summary() {
    fmt::print("[{}] ns: n={} dropped={} min={} p50={} p90={} p99={} p99.9={} p99.99={} max={}\n", m_name,
               count(), dropped(), min(), percentile(50.0), percentile(90.0), percentile(99.0),
               percentile(99.9), percentile(99.99), max());
    if (m_worstRun.n > 0) {
        fmt::print("[{} worst] {}\n", m_name, describeWorst(worstRun()));
    }
}

RecorderThread::RecorderThread(std::vector<LatencyRecorder*> recorders, int cpuCore, FlushConfig flush)
    : m_thread([recs = std::move(recorders), cpuCore, fl = std::move(flush)](const std::stop_token& stop) {
          loop(stop, recs, cpuCore, fl);
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
                          int core, const FlushConfig& flush) noexcept {
    (void)util::pinThread(core);
    util::HistogramLog log(flush.logFile);
    const bool         flushing      = log.open() && flush.intervalNs > 0;
    std::uint64_t      intervalStart = monotonicNs();
    std::uint64_t      nextFlush     = intervalStart + flush.intervalNs;
    std::uint32_t      sinceClock    = 0;
    while (!stop.stop_requested()) {
        bool any = false;
        for (LatencyRecorder* r : recorders) {
            any |= r->drainOne();
        }
        if (!any) {
            relax();
        }
        if (flushing && (!any || ++sinceClock == kDrainsPerClockRead)) {
            sinceClock              = 0;
            const std::uint64_t now = monotonicNs();
            if (now >= nextFlush) {
                flushInterval(log, recorders, intervalStart, now);
                intervalStart = now;
                nextFlush     = now + flush.intervalNs;
            }
        }
    }
    for (LatencyRecorder* r : recorders) {
        (void)r->drain();
    }
    if (flushing) {
        flushInterval(log, recorders, intervalStart, monotonicNs());
    }
}

void RecorderThread::flushInterval(util::HistogramLog& log, const std::vector<LatencyRecorder*>& recorders,
                                   std::uint64_t startNs, std::uint64_t endNs) noexcept {
    const double intervalSec = static_cast<double>(endNs - startNs) * 1e-9;
    const double startEpoch  = epochSeconds() - intervalSec;
    for (LatencyRecorder* r : recorders) {
        const util::Histogram& h = r->interval();
        if (h.count() > 0) {
            (void)log.writeInterval(r->name(), startEpoch, intervalSec, h);
            (void)log.writeComment(fmt::format("worst {}: {}", r->name(), describeWorst(r->worstInterval())));
            fmt::print("[{} +{:.0f}s] ns: n={} p50={} p99={} p99.9={} max={}\n", r->name(), intervalSec,
                       h.count(), h.percentile(50.0), h.percentile(99.0), h.percentile(99.9), h.max());
        }
        r->resetInterval();
    }
    log.flush();
}

}   // namespace abt::dut
