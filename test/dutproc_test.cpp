//
// In-process compute-latency probe for the DUT hot path (abt::dut::DutSession::proc()): push a
// realistic add/delete MoldUDP64 feed through the book builder and report the RX -> decision
// latency distribution (parse + book rebuild + strategy), with NO hardware timestamps. This is the
// pure data-structure/algorithm cost — the thing we want to prove is ultra-low-latency. Run the
// release build for meaningful numbers (ASan inflates them ~10-50x).
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <fmt/format.h>

#include "TestHarness.hpp"

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/DutSession.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/util/Tsc.hpp"

using namespace abt;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& msg) {
    return {reinterpret_cast<const std::byte*>(&msg), sizeof msg};
}

itch::AddOrder mkAdd(OrderId ref, char side, Quantity shares, Price price) {
    itch::AddOrder a{};
    a.messageType = 'A';
    a.orderRef = ref;
    a.side = side;
    a.shares = shares;
    a.price = static_cast<std::uint32_t>(price);
    return a;
}

itch::OrderDelete mkDelete(OrderId ref) {
    itch::OrderDelete d{};
    d.messageType = 'D';
    d.orderRef = ref;
    return d;
}

// Never trades — isolates the book-building/parse cost the strategy reacts to.
struct NeverSend {
    dut::OrderIntent onBook(const dut::BookBuilder&) noexcept {
        return {};
    }
};

void bench_proc() {
    dut::DutConfig cfg{};
    cfg.minPrice = 0;
    cfg.maxPrice = 100000;
    cfg.tickWire = 100;
    cfg.symbol = "ABT";
    cfg.t2tCapacity = 1u << 20;   // keep every sample for accurate percentiles
    dut::DutSession<dut::IoMode::Loopback, NeverSend> sess(cfg, NeverSend{});

    constexpr std::uint64_t kPackets = 50'000;
    constexpr std::uint64_t kWindow  = 256;   // steady-state live-order depth
    constexpr std::uint64_t kLevels  = 32;

    std::array<std::byte, 512> buf{};
    mold::Packer packer("BENCHSESS", 1);

    for (std::uint64_t i = 0; i < kPackets; ++i) {
        packer.reset(buf.data(), buf.size());
        Price price = 0;
        char side = 0;
        if ((i & 1u) == 0u) {
            side = 'B';
            price = 4000 + static_cast<Price>((i % kLevels) * 100);
        } else {
            side = 'S';
            price = 8000 + static_cast<Price>((i % kLevels) * 100);
        }
        (void)packer.append(bytesOf(mkAdd(i + 1, side, 100u, price)));
        if (i >= kWindow) {
            (void)packer.append(bytesOf(mkDelete(i + 1 - kWindow)));
        }
        sess.onMarketData(packer.finalize(), 0);
    }

    CHECK_EQ(sess.proc().count(), kPackets);
    sess.proc().summary("dut-proc rx->decision (parse+book+strategy)");
}

template <class T>
void pushMsg(std::vector<std::array<std::byte, 40>>& msgs, std::vector<std::size_t>& lens,
             const T& m) {
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
        const double perMsg =
            static_cast<double>(tsc::toNs(t1 - t0)) / static_cast<double>(msgs.size());
        if (perMsg < best) {
            best = perMsg;
        }
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
    std::vector<std::size_t> lens;
    msgs.reserve(kAdds * 2);
    lens.reserve(kAdds * 2);
    for (std::uint64_t i = 0; i < kAdds; ++i) {
        char side = 0;
        Price price = 0;
        if ((i & 1u) == 0u) {
            side = 'B';
            price = 4000 + static_cast<Price>((i % kLevels) * 100);
        } else {
            side = 'S';
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
        fmt::print("[dut-book] map={:>6} slots ({:>5} KB): {:.2f} ns/msg\n",
                   s, (s * 24u) / 1024u, perMsg);
    }
}

}

int main() {
    bench_proc();
    bench_book_throughput();
    return abt::test::summary("dutproc");
}
