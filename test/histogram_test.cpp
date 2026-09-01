//
// Smoke test for the HdrHistogram binding (abt::util::Histogram): confirms the vendored library is
// wired up and the wrapper records + reports percentiles within HdrHistogram's precision.
//

#include "TestHarness.hpp"

#include <cstdint>

#include "t2t/util/Histogram.hpp"

using namespace abt;

namespace {

void test_histogram() {
    util::Histogram h(1, 1'000'000'000, 3);   // 1 ns .. 1 s, 3 significant figures
    CHECK_EQ(h.count(), 0);
    CHECK_EQ(h.min(), 0);
    CHECK_EQ(h.max(), 0);

    for (std::int64_t v = 1; v <= 1000; ++v) {
        h.record(v);
    }
    CHECK_EQ(h.count(), 1000);
    CHECK_EQ(h.min(), 1);
    CHECK(h.max() >= 999 && h.max() <= 1001);

    const std::int64_t p50 = h.percentile(50.0);
    CHECK(p50 >= 495 && p50 <= 505);
    const std::int64_t p99 = h.percentile(99.0);
    CHECK(p99 >= 985 && p99 <= 995);

    h.reset();
    CHECK_EQ(h.count(), 0);
    CHECK_EQ(h.percentile(50.0), 0);
}

}   // namespace

int main() {
    test_histogram();
    return abt::test::summary("histogram");
}
