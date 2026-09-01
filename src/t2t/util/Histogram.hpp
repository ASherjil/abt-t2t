#pragma once
//
// Thin RAII binding over the official HdrHistogram (HdrHistogram_c). The consumer thread of the
// latency pipeline records nanosecond samples into one of these and queries percentiles at the end;
// HdrHistogram keeps a fixed-memory, high-dynamic-range log/linear bucketing so it stays accurate
// from nanoseconds to seconds without storing every sample. The C struct is forward-declared so the
// HdrHistogram header stays contained in Histogram.cpp.
//

#include <cstdint>
#include <memory>

struct hdr_histogram;

namespace abt::util {

// Custom deletor for the std::unique_ptr<T, Deletor>
struct HdrClose {
    void operator()(hdr_histogram* hdr) const noexcept;
};

class Histogram {
public:
    // Track [lowest, highest] (same unit as the samples, e.g. ns) with `sigFigs` significant digits
    // of precision (1-5; 3 is the usual choice).
    Histogram(std::int64_t lowest, std::int64_t highest, int sigFigs);

    void record(std::int64_t value) noexcept;

    [[nodiscard]] std::int64_t percentile(double p) const noexcept;   // p in [0, 100]
    [[nodiscard]] std::int64_t min() const noexcept;
    [[nodiscard]] std::int64_t max() const noexcept;
    [[nodiscard]] double       mean() const noexcept;
    [[nodiscard]] std::int64_t count() const noexcept;

    void reset() noexcept;

private:
    std::unique_ptr<hdr_histogram, HdrClose> m_h;
};

}   // namespace abt::util
