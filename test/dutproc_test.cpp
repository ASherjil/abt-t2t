//
// In-process compute-latency probe for the DUT hot path (abt::dut::DutSession::proc()): push a
// realistic add/delete MoldUDP64 feed through the book builder and report the RX -> decision
// latency distribution (parse + book rebuild + strategy), with NO hardware timestamps. This is the
// pure data-structure/algorithm cost — the thing we want to prove is ultra-low-latency. Run the
// release build for meaningful numbers (ASan inflates them ~10-50x).
//

#include "TestHarness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <fmt/format.h>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/DutSession.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/util/Scan.hpp"
#include "t2t/util/Tsc.hpp"

using namespace abt;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& msg) {
    return {reinterpret_cast<const std::byte*>(&msg), sizeof msg};
}

itch::AddOrder mkAdd(OrderId ref, char side, Quantity shares, Price price) {
    itch::AddOrder a{};
    a.messageType = itch::MessageType::AddOrder;
    a.orderRef    = ref;
    a.side        = static_cast<itch::Side>(side);
    a.shares      = shares;
    a.price       = static_cast<std::uint32_t>(price);
    return a;
}

itch::OrderDelete mkDelete(OrderId ref) {
    itch::OrderDelete d{};
    d.messageType = itch::MessageType::OrderDelete;
    d.orderRef    = ref;
    return d;
}

struct NeverSend {
    static dut::QuoteTargets onBook(const dut::BookBuilder&, const dut::Account&) noexcept {
        return {};
    }
};

struct BidOnce {
    bool armed = true;

    dut::QuoteTargets onBook(const dut::BookBuilder& book, const dut::Account&) noexcept {
        dut::QuoteTargets t{};
        if (!armed || book.bestAsk() == kNoPrice) {
            return t;
        }
        armed      = false;
        t.quoteBid = true;
        t.bidPrice = book.bestAsk();
        t.bidQty   = 5u;
        return t;
    }
};

void test_sw_recorders() {
    dut::DutConfig cfg{};
    cfg.minPrice = 0;
    cfg.maxPrice = 100000;
    cfg.tickWire = 100;
    cfg.symbol   = "ABT";
    dut::DutSession<dut::IoMode::Loopback, BidOnce> sess(cfg, BidOnce{});

    std::array<std::byte, 512> buf{};
    mold::Packer               packer("BENCHSESS", 1);
    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 100u, 4000)));
    sess.onMarketData(packer.finalize(), 0);
    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 100u, 4100)));
    sess.onMarketData(packer.finalize(), 0);
    CHECK_EQ(sess.ordersSent(), 1u);

    (void)sess.proc().drain();
    CHECK_EQ(sess.proc().count(), 2);
    (void)sess.t2tSw().drain();
    CHECK_EQ(sess.t2tSw().count(), 1);
    CHECK(sess.t2tSw().min() > 0 && sess.t2tSw().min() < 1'000'000);
}

void bench_proc() {
    dut::DutConfig cfg{};
    cfg.minPrice      = 0;
    cfg.maxPrice      = 100000;
    cfg.tickWire      = 100;
    cfg.symbol        = "ABT";
    cfg.queueCapacity = 1u << 17;
    dut::DutSession<dut::IoMode::Loopback, NeverSend> sess(cfg, NeverSend{});

    constexpr std::uint64_t kPackets = 50'000;
    constexpr std::uint64_t kWindow  = 256;   // steady-state live-order depth
    constexpr std::uint64_t kLevels  = 32;

    std::array<std::byte, 512> buf{};
    mold::Packer               packer("BENCHSESS", 1);

    for (std::uint64_t i = 0; i < kPackets; ++i) {
        packer.reset(buf.data(), buf.size());
        Price price = 0;
        char  side  = 0;
        if ((i & 1u) == 0u) {
            side  = 'B';
            price = 4000 + static_cast<Price>((i % kLevels) * 100);
        } else {
            side  = 'S';
            price = 8000 + static_cast<Price>((i % kLevels) * 100);
        }
        (void)packer.append(bytesOf(mkAdd(i + 1, side, 100u, price)));
        if (i >= kWindow) {
            (void)packer.append(bytesOf(mkDelete(i + 1 - kWindow)));
        }
        sess.onMarketData(packer.finalize(), 0);
    }

    (void)sess.proc().drain();
    CHECK_EQ(sess.proc().count(), kPackets);
    CHECK_EQ(sess.proc().dropped(), 0u);
    sess.proc().summary();
}

template <class T>
void pushMsg(std::vector<std::array<std::byte, 40>>& msgs, std::vector<std::size_t>& lens, const T& m) {
    std::array<std::byte, 40> buf{};
    std::memcpy(buf.data(), &m, sizeof m);
    msgs.push_back(buf);
    lens.push_back(sizeof m);
}

double runBookThroughput(std::size_t maxOrders, const std::vector<std::array<std::byte, 40>>& msgs,
                         const std::vector<std::size_t>& lens) {
    dut::BookBuilder book(0, 100000, 100, maxOrders);
    // Take the fastest of a few repeats: a mid-loop deschedule only ever inflates the time, so the
    // minimum is the cleanest estimate of the actual compute cost.
    double best = 1.0e30;
    for (int rep = 0; rep < 5; ++rep) {
        const std::uint64_t t0 = tsc::now();
        for (std::size_t k = 0; k < msgs.size(); ++k) {
            book.apply({msgs[k].data(), lens[k]});
        }
        const std::uint64_t t1 = tsc::now();
        const double perMsg    = static_cast<double>(tsc::toNs(t1 - t0)) / static_cast<double>(msgs.size());
        best                   = std::min(perMsg, best);
    }
    CHECK(book.liveOrders() > 0);
    return best;
}

// Pure book.apply() throughput: pre-generate the messages, then time the tight apply loop with a
// single timer pair so the per-op cycle-counter probe is amortised to ~0. This is the sensitive
// signal for data-structure work (the per-packet proc() distribution carries ~10-15ns of probe
// overhead per sample and swings with OS noise on a non-isolated box). Sweeping the order-map size
// exposes how much of the cost is cache footprint (the live set is ~256 orders).
void bench_book_throughput() {
    tsc::warmUp();

    constexpr std::uint64_t kAdds   = 50'000;
    constexpr std::uint64_t kWindow = 256;
    constexpr std::uint64_t kLevels = 32;

    std::vector<std::array<std::byte, 40>> msgs;
    std::vector<std::size_t>               lens;
    msgs.reserve(kAdds * 2);
    lens.reserve(kAdds * 2);
    for (std::uint64_t i = 0; i < kAdds; ++i) {
        char  side  = 0;
        Price price = 0;
        if ((i & 1u) == 0u) {
            side  = 'B';
            price = 4000 + static_cast<Price>((i % kLevels) * 100);
        } else {
            side  = 'S';
            price = 8000 + static_cast<Price>((i % kLevels) * 100);
        }
        pushMsg(msgs, lens, mkAdd(i + 1, side, 100u, price));
        if (i >= kWindow) {
            pushMsg(msgs, lens, mkDelete(i + 1 - kWindow));
        }
    }

    const std::size_t sizes[] = {512u, 1024u, 2048u, 4096u, 16384u, 65536u};
    for (std::size_t s : sizes) {
        const double perMsg = runBookThroughput(s, msgs, lens);
        fmt::print("[dut-book] map={:>6} slots ({:>5} KB): {:.2f} ns/msg\n", s, (s * 24u) / 1024u, perMsg);
    }
}

// Worst-case best-price rescan: a mostly-empty ladder with the fallback level far away, so the
// scan traverses almost the whole array. Compares the scalar path to the dispatched (AVX2) path.
void bench_scan() {
    tsc::warmUp();
    constexpr std::size_t      kLevels = 1u << 16;
    std::vector<std::uint32_t> ladder(kLevels, 0u);
    ladder[3]            = 1u;   // one populated level near the bottom
    constexpr int kIters = 20000;

    std::size_t         sink = 0;
    const std::uint64_t s0   = tsc::now();
    for (int r = 0; r < kIters; ++r) {
        sink += util::scanDownNonZeroScalar(ladder.data(), kLevels - 1 - static_cast<std::size_t>(r & 7));
    }
    const std::uint64_t s1       = tsc::now();
    const double        scalarNs = static_cast<double>(tsc::toNs(s1 - s0)) / kIters;

    const std::uint64_t v0 = tsc::now();
    for (int r = 0; r < kIters; ++r) {
        sink += util::scanDownNonZero(ladder.data(), kLevels - 1 - static_cast<std::size_t>(r & 7));
    }
    const std::uint64_t v1     = tsc::now();
    const double        simdNs = static_cast<double>(tsc::toNs(v1 - v0)) / kIters;

    fmt::print("[dut-scan] rescan {} empty levels: scalar {:.0f} ns, simd {:.0f} ns ({:.1f}x)\n", kLevels,
               scalarNs, simdNs, simdNs > 0.0 ? scalarNs / simdNs : 0.0);
    CHECK(sink > 0);
}

}   // namespace

int main() {
    test_sw_recorders();
    bench_proc();
    bench_book_throughput();
    bench_scan();
    return abt::test::summary("dutproc");
}
