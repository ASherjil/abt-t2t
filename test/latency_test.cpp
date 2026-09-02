#include "TestHarness.hpp"

#include <cstdint>

#include "t2t/dut/LatencyRecorder.hpp"
#include "t2t/dut/SampleContext.hpp"

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

void test_worst_samples_keep_context() {
    dut::LatencyRecorder rec("worst", 4096, 1.0, 3);
    for (std::uint64_t v = 1; v <= 100; ++v) {
        rec.record(v, dut::SampleContext::pack(1000 + v, 3, 0));
    }
    rec.record(5000,
               dut::SampleContext::pack(77, 2, dut::SampleContext::kSent | dut::SampleContext::kRehash));
    (void)rec.drain();

    const auto worst = rec.worstRun();
    CHECK_EQ(worst.size(), dut::LatencyRecorder::kWorst);
    CHECK_EQ(worst[0].ns, 5000);
    CHECK_EQ(dut::SampleContext::seq(worst[0].ctx), 77u);
    CHECK_EQ(dut::SampleContext::msgs(worst[0].ctx), 2u);
    CHECK_EQ(dut::SampleContext::flags(worst[0].ctx),
             dut::SampleContext::kSent | dut::SampleContext::kRehash);
    CHECK_EQ(worst[1].ns, 100);
    CHECK_EQ(worst[worst.size() - 1].ns, 94);

    rec.resetInterval();
    CHECK_EQ(rec.worstInterval().size(), 0u);
    CHECK_EQ(rec.worstRun().size(), dut::LatencyRecorder::kWorst);
}

void test_consumer_thread() {
    dut::LatencyRecorder a("a", 1u << 12, 1.0, 3);
    dut::LatencyRecorder b("b", 1u << 12, 1.0, 3);
    dut::RecorderThread  consumer({&a, &b}, -1);
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
    test_worst_samples_keep_context();
    test_drain_and_percentiles();
    test_unit_conversion();
    test_overflow_counts_drops();
    test_consumer_thread();
    return abt::test::summary("latency");
}
