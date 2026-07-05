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
#include <span>

#include "TestHarness.hpp"

#include "abt/dut/DutSession.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/MoldUdp64.hpp"

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

}

int main() {
    bench_proc();
    return abt::test::summary("dutproc");
}
