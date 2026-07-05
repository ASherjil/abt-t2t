//
// Unit test for the tick-to-trade latency recorder (abt::dut::T2tRecorder).
//

#include <cstdint>

#include "TestHarness.hpp"

#include "abt/dut/T2tRecorder.hpp"

using namespace abt;

namespace {

void test_percentiles() {
    dut::T2tRecorder rec(4096);
    for (std::uint64_t v = 0; v < 100; ++v) {
        rec.record(v);
    }
    CHECK_EQ(rec.count(), 100u);
    CHECK_EQ(rec.min(), 0u);
    CHECK_EQ(rec.max(), 99u);
    CHECK_EQ(rec.percentile(0.0), 0u);
    CHECK_EQ(rec.percentile(0.50), 50u);
    CHECK_EQ(rec.percentile(0.90), 89u);
    CHECK_EQ(rec.percentile(0.99), 98u);
    CHECK_EQ(rec.percentile(1.0), 99u);
}

void test_capacity_and_clear() {
    // Storage caps at capacity, but count/min/max still track every sample.
    dut::T2tRecorder rec(4);
    rec.record(500);
    rec.record(100);
    rec.record(900);
    rec.record(300);
    rec.record(700);
    rec.record(50);
    CHECK_EQ(rec.count(), 6u);
    CHECK_EQ(rec.min(), 50u);
    CHECK_EQ(rec.max(), 900u);

    rec.clear();
    CHECK_EQ(rec.count(), 0u);
    CHECK_EQ(rec.percentile(0.50), 0u);
}

}

int main() {
    test_percentiles();
    test_capacity_and_clear();
    return abt::test::summary("t2t");
}
