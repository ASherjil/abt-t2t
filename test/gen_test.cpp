//
// gen_test.cpp -- the synthetic flow generator keeps a sane two-sided book, emits ITCH
// (never OUCH), and is deterministic for a fixed seed.
//
#include <cstddef>
#include <span>

#include "abt/sim/FlowGenerator.hpp"
#include "abt/sim/Venue.hpp"
#include "TestHarness.hpp"

using namespace abt;

namespace {

struct CountSink {
    std::size_t md = 0;
    std::size_t oe = 0;
    void marketData(std::span<const std::byte>) { ++md; }
    void orderEntry(std::span<const std::byte>) { ++oe; }
};

void test_generator_book_invariant() {
    CountSink sink;
    sim::Venue<CountSink> v(sink, "AAPL", 1, 1, 100000, 100);
    sim::FlowGenerator<sim::Venue<CountSink>> gen(v, {});

    for (int i = 0; i < 2000; ++i) {
        gen.step(1'000'000 + static_cast<std::uint64_t>(i) * 100);
        // The matching engine must never leave a crossed book.
        if (v.bestBid() != lob::kNoPrice && v.bestAsk() != lob::kNoPrice) {
            CHECK(v.bestBid() < v.bestAsk());
        }
    }
    CHECK(sink.md > 0);      // produced market data
    CHECK_EQ(sink.oe, 0u);   // synthetic flow never emits OUCH
}

void test_generator_deterministic() {
    CountSink s1, s2;
    sim::Venue<CountSink> v1(s1, "AAPL", 1, 1, 100000, 100);
    sim::Venue<CountSink> v2(s2, "AAPL", 1, 1, 100000, 100);

    sim::FlowGenerator<sim::Venue<CountSink>>::Config cfg{};
    cfg.seed = 0xABCDEF12345ull;
    sim::FlowGenerator<sim::Venue<CountSink>> g1(v1, cfg);
    sim::FlowGenerator<sim::Venue<CountSink>> g2(v2, cfg);

    g1.run(1500, 0, 100);
    g2.run(1500, 0, 100);

    CHECK_EQ(s1.md, s2.md);                 // identical output
    CHECK_EQ(v1.bestBid(), v2.bestBid());   // identical book
    CHECK_EQ(v1.bestAsk(), v2.bestAsk());
}

}  // namespace

int main() {
    test_generator_book_invariant();
    test_generator_deterministic();
    return abt::test::summary("gen_test");
}
