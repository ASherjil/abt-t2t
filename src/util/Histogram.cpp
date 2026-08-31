//
// HdrHistogram RAII binding — definitions.
//

#include "abt/util/Histogram.hpp"

#include <hdr/hdr_histogram.h>

namespace abt::util {

Histogram::Histogram(std::int64_t lowest, std::int64_t highest, int sigFigs) {
    // hdr_init allocates; on failure m_h stays null and every method degrades to a no-op.
    (void)hdr_init(lowest, highest, sigFigs, &m_h);
}

Histogram::~Histogram() {
    if (m_h != nullptr) {
        hdr_close(m_h);
    }
}

void Histogram::record(std::int64_t value) noexcept {
    if (m_h != nullptr) {
        (void)hdr_record_value(m_h, value);
    }
}

std::int64_t Histogram::percentile(double p) const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_value_at_percentile(m_h, p);
}

std::int64_t Histogram::min() const noexcept {
    if (m_h == nullptr || m_h->total_count == 0) {
        return 0;
    }
    return hdr_min(m_h);
}

std::int64_t Histogram::max() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_max(m_h);
}

double Histogram::mean() const noexcept {
    if (m_h == nullptr) {
        return 0.0;
    }
    return hdr_mean(m_h);
}

std::int64_t Histogram::count() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return m_h->total_count;
}

void Histogram::reset() noexcept {
    if (m_h != nullptr) {
        hdr_reset(m_h);
    }
}

}
