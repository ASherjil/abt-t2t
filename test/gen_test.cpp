//
// Flow generator keeps an uncrossed book, emits only ITCH, and is deterministic.
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
    Venue<CountSink> v(sink, "AAPL", 1, 1, 100000, 100);
    FlowGenerator<Venue<CountSink>> gen(v, {});

    for (int i = 0; i < 2000; ++i) {
        gen.step(1'000'000 + static_cast<std::uint64_t>(i) * 100);
        if (v.bestBid() != kNoPrice && v.bestAsk() != kNoPrice) {
            CHECK(v.bestBid() < v.bestAsk());
        }
    }
    CHECK(sink.md > 0);
    CHECK_EQ(sink.oe, 0u);
}

void test_generator_deterministic() {
    CountSink s1, s2;
    Venue<CountSink> v1(s1, "AAPL", 1, 1, 100000, 100);
    Venue<CountSink> v2(s2, "AAPL", 1, 1, 100000, 100);

    FlowConfig cfg{};
    cfg.seed = 0xABCDEF12345ull;
    FlowGenerator<Venue<CountSink>> g1(v1, cfg);
    FlowGenerator<Venue<CountSink>> g2(v2, cfg);

    g1.run(1500, 0, 100);
    g2.run(1500, 0, 100);

    CHECK_EQ(s1.md, s2.md);
    CHECK_EQ(v1.bestBid(), v2.bestBid());
    CHECK_EQ(v1.bestAsk(), v2.bestAsk());
}

}

int main() {
    test_generator_book_invariant();
    test_generator_deterministic();
    return abt::test::summary("gen_test");
}
