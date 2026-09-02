//
// HdrHistogram RAII binding — definitions.
//

#include "t2t/util/Histogram.hpp"

#include <hdr/hdr_histogram.h>

namespace abt::util {

void HdrClose::operator()(hdr_histogram* hdr) const noexcept {
    hdr_close(hdr);
}

Histogram::Histogram(std::int64_t lowest, std::int64_t highest, int sigFigs) {
    // hdr_init allocates; on failure m_h stays null and every method degrades to a no-op.
    hdr_histogram* h{};
    (void)hdr_init(lowest, highest, sigFigs, &h);
    m_h.reset(h);
}

void Histogram::record(std::int64_t value) noexcept {
    if (m_h) {
        (void)hdr_record_value(m_h.get(), value);
    }
}

std::int64_t Histogram::percentile(double p) const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_value_at_percentile(m_h.get(), p);
}

std::int64_t Histogram::min() const noexcept {
    if (m_h == nullptr || m_h->total_count == 0) {
        return 0;
    }
    return hdr_min(m_h.get());
}

std::int64_t Histogram::max() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_max(m_h.get());
}

double Histogram::mean() const noexcept {
    if (m_h == nullptr) {
        return 0.0;
    }
    return hdr_mean(m_h.get());
}

std::int64_t Histogram::count() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return m_h->total_count;
}

void Histogram::reset() noexcept {
    if (m_h) {
        hdr_reset(m_h.get());
    }
}

}   // namespace abt::util
