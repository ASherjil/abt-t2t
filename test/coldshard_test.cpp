#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "t2t/dut/ColdShard.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/MoldUdp64.hpp"

using namespace abt;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& msg) {
    return {reinterpret_cast<const std::byte*>(&msg), sizeof msg};
}

itch::StockDirectory mkDir(std::uint16_t locate, std::string_view name) {
    itch::StockDirectory r{};
    r.messageType = itch::MessageType::StockDirectory;
    r.stockLocate = locate;
    r.stock       = name;
    return r;
}

itch::AddOrder mkAdd(std::uint16_t locate, OrderId ref, char side, Quantity shares, Price price) {
    itch::AddOrder a{};
    a.messageType = itch::MessageType::AddOrder;
    a.stockLocate = locate;
    a.orderRef    = ref;
    a.side        = static_cast<itch::Side>(side);
    a.shares      = shares;
    a.price       = static_cast<std::uint32_t>(price);
    return a;
}

itch::SystemEvent mkSys(char code) {
    itch::SystemEvent s{};
    s.messageType = itch::MessageType::SystemEvent;
    s.eventCode   = static_cast<itch::SystemEventCode>(code);
    return s;
}

dut::BookTableConfig coldCfg() {
    dut::BookTableConfig cfg{};
    cfg.tickWire      = 100;
    cfg.coldBandTicks = 512;
    cfg.hotBandTicks  = 512;
    cfg.bandFraction  = 0.0;
    cfg.coldMapSlots  = 64;
    cfg.hotMapSlots   = 64;
    cfg.hotSymbols    = {"AAPL"};
    cfg.scope         = dut::BookScope::ColdOnly;
    return cfg;
}

struct Frames {
    std::vector<std::array<std::byte, 512>> bufs;
    mold::Packer                            packer{"SESSION01", 1};
    std::size_t                             next = 0;

    explicit Frames(std::size_t n) : bufs(n) {
    }

    template <class... Msgs>
    std::span<const std::byte> packet(const Msgs&... msgs) {
        auto& b = bufs[next++];
        packer.reset(b.data(), b.size());
        (static_cast<void>(packer.append(bytesOf(msgs))), ...);
        return packer.finalize();
    }
};

void test_shard_books_cold_symbols_from_frames_and_resets() {
    Frames         frames(8);
    dut::ColdShard shard(coldCfg(), 2048, -1);
    shard.start();
    CHECK(shard.running());
    CHECK(shard.push(frames.packet(mkDir(13, "AAPL"), mkDir(99, "COLD"))));
    CHECK(shard.push(frames.packet(mkAdd(99, 1u, 'B', 10u, 500000), mkAdd(99, 2u, 'S', 10u, 510000),
                                   mkAdd(13, 3u, 'B', 10u, 320000))));
    shard.stop();
    CHECK(!shard.running());
    CHECK_EQ(shard.packets(), 2u);
    CHECK_EQ(shard.applied(), 5u);
    CHECK_EQ(shard.dropped(), 0u);
    CHECK_EQ(shard.stale(), 0u);
    const dut::BookTable& b = shard.books();
    CHECK_EQ(b.symbols(), 1u);
    CHECK(b.book(99) != nullptr && b.book(99)->bestBid() == 500000 && b.book(99)->bestAsk() == 510000);
    CHECK(b.book(13) == nullptr);
    CHECK_EQ(b.hotIndexOf(13), 0);
    CHECK_EQ(b.liveOrders(), 2u);

    shard.start();
    shard.reset();
    CHECK(shard.push(frames.packet(mkAdd(99, 4u, 'B', 5u, 490000))));
    shard.stop();
    CHECK_EQ(shard.books().liveOrders(), 1u);
    CHECK_EQ(shard.books().book(99)->bestBid(), 490000);
    CHECK_EQ(shard.books().book(99)->bestAsk(), kNoPrice);

    shard.start();
    CHECK(shard.push(frames.packet(mkSys('O'), mkAdd(99, 5u, 'S', 5u, 520000))));
    shard.stop();
    CHECK_EQ(shard.books().liveOrders(), 1u);
    CHECK_EQ(shard.books().book(99)->bestBid(), kNoPrice);
    CHECK_EQ(shard.books().book(99)->bestAsk(), 520000);
}

void test_full_ring_drops_and_deep_backlog_is_stale() {
    Frames         frames(32);
    dut::ColdShard shard(coldCfg(), 1032, -1);
    for (unsigned i = 0; i < 20; ++i) {
        (void)shard.push(frames.packet(mkAdd(99, i + 1, 'B', 1u, 500000)));
    }
    CHECK_EQ(shard.dropped(), 0u);
    shard.start();
    shard.stop();
    CHECK_EQ(shard.stale(), 12u);
    CHECK_EQ(shard.packets(), 8u);
    CHECK_EQ(shard.books().liveOrders(), 8u);

    dut::ColdShard tiny(coldCfg(), 4, -1);
    for (unsigned i = 0; i < 8; ++i) {
        (void)tiny.push(frames.packet(mkAdd(99, i + 1, 'B', 1u, 500000)));
    }
    CHECK(tiny.dropped() >= 4u);
}

}   // namespace

int main() {
    test_shard_books_cold_symbols_from_frames_and_resets();
    test_full_ring_drops_and_deep_backlog_is_stale();
    return abt::test::summary("coldshard");
}
