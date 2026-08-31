#include "TestHarness.hpp"

#include <cstdint>

#include "abt/dut/LatencyRecorder.hpp"

using namespace abt;

namespace {

void test_drain_and_percentiles() {
    dut::LatencyRecorder rec("unit", 4096, 1.0, 3);
    for (std::uint64_t v = 1; v <= 1000; ++v) {
        rec.record(v);
    }
    CHECK_EQ(rec.count(), 0);
    CHECK_EQ(rec.drain(), 1000u);
    CHECK_EQ(rec.count(), 1000);
    CHECK_EQ(rec.min(), 1);
    CHECK(rec.max() >= 999 && rec.max() <= 1001);
    const std::int64_t p50 = rec.percentile(50.0);
    CHECK(p50 >= 495 && p50 <= 505);
    CHECK_EQ(rec.dropped(), 0u);
}

void test_unit_conversion() {
    dut::LatencyRecorder rec("cycles", 64, 0.5, 3);
    rec.record(2000);
    rec.record(4000);
    (void)rec.drain();
    CHECK_EQ(rec.count(), 2);
    CHECK(rec.min() >= 999 && rec.min() <= 1001);
    CHECK(rec.max() >= 1999 && rec.max() <= 2001);
}

void test_overflow_counts_drops() {
    dut::LatencyRecorder rec("small", 4, 1.0, 3);
    for (std::uint64_t v = 1; v <= 10; ++v) {
        rec.record(v);
    }
    CHECK_EQ(rec.dropped(), 6u);
    CHECK_EQ(rec.drain(), 4u);
    CHECK_EQ(rec.count(), 4);
}

void test_consumer_thread() {
    dut::LatencyRecorder a("a", 1u << 12, 1.0, 3);
    dut::LatencyRecorder b("b", 1u << 12, 1.0, 3);
    dut::RecorderThread  consumer({&a, &b}, -1);
    consumer.start();
    CHECK(consumer.running());
    for (std::uint64_t v = 1; v <= 100000; ++v) {
        a.record(v % 1000 + 1);
        if ((v & 7u) == 0u) {
            b.record(v % 50 + 1);
        }
    }
    consumer.stop();
    CHECK(!consumer.running());
    CHECK_EQ(a.count() + static_cast<std::int64_t>(a.dropped()), 100000);
    CHECK_EQ(b.count() + static_cast<std::int64_t>(b.dropped()), 12500);
    CHECK(a.max() >= 999 && a.max() <= 1001);
}

}   // namespace

int main() {
    test_drain_and_percentiles();
    test_unit_conversion();
    test_overflow_counts_drops();
    test_consumer_thread();
    return abt::test::summary("latency");
}
