//
// Unit test for the DUT feed-driven order book (abt::dut::BookBuilder).
//

#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/protocol/Itch50.hpp"

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

itch::OrderExecuted mkExec(OrderId ref, Quantity executed) {
    itch::OrderExecuted e{};
    e.messageType    = itch::MessageType::OrderExecuted;
    e.orderRef       = ref;
    e.executedShares = executed;
    return e;
}

itch::OrderCancel mkCancel(OrderId ref, Quantity cancelled) {
    itch::OrderCancel x{};
    x.messageType     = itch::MessageType::OrderCancel;
    x.orderRef        = ref;
    x.cancelledShares = cancelled;
    return x;
}

itch::OrderDelete mkDelete(OrderId ref) {
    itch::OrderDelete d{};
    d.messageType = itch::MessageType::OrderDelete;
    d.orderRef    = ref;
    return d;
}

itch::OrderReplace mkReplace(OrderId oldRef, OrderId newRef, Quantity shares, Price price) {
    itch::OrderReplace r{};
    r.messageType  = itch::MessageType::OrderReplace;
    r.origOrderRef = oldRef;
    r.newOrderRef  = newRef;
    r.shares       = shares;
    r.price        = static_cast<std::uint32_t>(price);
    return r;
}

void test_book() {
    dut::BookBuilder book(1, 1000, 1);

    book.apply(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    book.apply(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    CHECK_EQ(book.bestBid(), 100);
    CHECK_EQ(book.bestAsk(), 102);
    CHECK_EQ(book.sizeAt(Side::Buy, 100), 500u);
    CHECK_EQ(book.sizeAt(Side::Sell, 102), 300u);
    CHECK_EQ(book.liveOrders(), 2u);

    book.apply(bytesOf(mkAdd(3u, 'B', 200u, 101)));
    CHECK_EQ(book.bestBid(), 101);
    CHECK_EQ(book.sizeAt(Side::Buy, 101), 200u);

    book.apply(bytesOf(mkExec(3u, 200u)));
    CHECK_EQ(book.sizeAt(Side::Buy, 101), 0u);
    CHECK_EQ(book.bestBid(), 100);
    CHECK_EQ(book.liveOrders(), 2u);

    book.apply(bytesOf(mkCancel(1u, 100u)));
    CHECK_EQ(book.sizeAt(Side::Buy, 100), 400u);
    CHECK_EQ(book.liveOrders(), 2u);

    book.apply(bytesOf(mkDelete(2u)));
    CHECK_EQ(book.bestAsk(), kNoPrice);
    CHECK_EQ(book.liveOrders(), 1u);

    book.apply(bytesOf(mkReplace(1u, 4u, 250u, 99)));
    CHECK_EQ(book.sizeAt(Side::Buy, 100), 0u);
    CHECK_EQ(book.sizeAt(Side::Buy, 99), 250u);
    CHECK_EQ(book.bestBid(), 99);
    CHECK_EQ(book.liveOrders(), 1u);
}

}   // namespace

void test_own_orders_excluded_from_view() {
    constexpr OrderId kOwn = 1ull << 62;
    dut::BookBuilder  b(1, 1000, 1, 64, kOwn);
    b.apply(bytesOf(mkAdd(1u, 'B', 100u, 100)));
    b.apply(bytesOf(mkAdd(2u, 'S', 100u, 103)));
    b.apply(bytesOf(mkAdd(kOwn, 'B', 50u, 101)));
    b.apply(bytesOf(mkAdd(kOwn + 1, 'S', 50u, 102)));
    CHECK_EQ(b.bestBid(), 100);
    CHECK_EQ(b.bestAsk(), 103);
    CHECK_EQ(b.sizeAt(Side::Buy, 101), 0u);
    CHECK_EQ(b.liveOrders(), 4u);
    CHECK_EQ(b.ownOrders(), 2u);
    CHECK_EQ(b.restingShares(kOwn), 50u);

    b.apply(bytesOf(mkExec(kOwn, 20u)));
    CHECK_EQ(b.restingShares(kOwn), 30u);
    CHECK_EQ(b.sizeAt(Side::Buy, 101), 0u);
    b.apply(bytesOf(mkReplace(kOwn, kOwn + 2, 30u, 99)));
    CHECK_EQ(b.bestBid(), 100);
    CHECK_EQ(b.sizeAt(Side::Buy, 99), 0u);
    CHECK_EQ(b.ownOrders(), 2u);
    b.apply(bytesOf(mkDelete(kOwn + 2)));
    b.apply(bytesOf(mkExec(kOwn + 1, 50u)));
    CHECK_EQ(b.ownOrders(), 0u);
    CHECK_EQ(b.liveOrders(), 2u);
    CHECK_EQ(b.bestBid(), 100);
    CHECK_EQ(b.bestAsk(), 103);

    b.apply(bytesOf(mkReplace(1u, 3u, 100u, 102)));
    CHECK_EQ(b.bestBid(), 102);
    b.clear();
    CHECK_EQ(b.liveOrders(), 0u);
    CHECK(b.bestBid() == kNoPrice);
}

void test_dynamic_band_anchors_on_first_add() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 16;
    cfg.bandFraction = 0.0;
    dut::BookBuilder book(cfg);
    CHECK(!book.anchored());
    CHECK_EQ(book.bestBid(), kNoPrice);

    book.apply(bytesOf(mkAdd(1u, 'B', 100u, 320000)));
    CHECK(book.anchored());
    CHECK_EQ(book.bandLow(), 320000 - 1600);
    CHECK_EQ(book.bandHigh(), 320000 + 1600);
    CHECK_EQ(book.bestBid(), 320000);
    CHECK_EQ(book.sizeAt(Side::Buy, 320000), 100u);

    book.apply(bytesOf(mkAdd(2u, 'S', 50u, 321600)));
    CHECK_EQ(book.bestAsk(), 321600);
    book.apply(bytesOf(mkAdd(3u, 'S', 70u, 400000)));
    CHECK_EQ(book.outOfBandAdds(), 1u);
    CHECK_EQ(book.liveOrders(), 3u);
    CHECK_EQ(book.bestAsk(), 321600);
    book.apply(bytesOf(mkDelete(3u)));
    CHECK_EQ(book.liveOrders(), 2u);

    book.clear();
    CHECK(book.anchored());
    CHECK_EQ(book.liveOrders(), 0u);
    CHECK_EQ(book.bestBid(), kNoPrice);
}

void test_dynamic_band_scales_with_price_and_clamps_at_zero() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 10;
    cfg.bandFraction = 0.10;
    cfg.maxBandTicks = 0;
    dut::BookBuilder pricey(cfg);
    pricey.apply(bytesOf(mkAdd(1u, 'B', 1u, 3'400'000'000 / 2)));
    CHECK_EQ(pricey.bandLow(), 1'700'000'000 - 1000);
    CHECK_EQ(pricey.bandHigh(), 1'700'000'000 + 1000);
    itch::TradeNonCross trade{};
    trade.messageType = itch::MessageType::TradeNonCross;
    trade.shares      = 1;
    trade.price       = 1'700'005'000;
    pricey.apply(bytesOf(trade));
    CHECK_EQ(pricey.reanchors(), 1u);
    CHECK_EQ(pricey.bandLow(), 1'700'005'000 - 170'000'500);
    CHECK_EQ(pricey.bandHigh(), 1'700'005'000 + 170'000'500);

    cfg.maxBandTicks = 100;
    dut::BookBuilder capped(cfg);
    capped.apply(bytesOf(mkAdd(1u, 'B', 1u, 1'000'000)));
    trade.price = 2'000'000;
    capped.apply(bytesOf(trade));
    CHECK_EQ(capped.bandLow(), 2'000'000 - 10'000);
    CHECK_EQ(capped.bandHigh(), 2'000'000 + 10'000);

    dut::BookBuilder penny(cfg);
    penny.apply(bytesOf(mkAdd(1u, 'B', 1u, 350)));
    CHECK_EQ(penny.bandLow(), 0);
    CHECK_EQ(penny.bandHigh(), 2000);
    CHECK_EQ(penny.sizeAt(Side::Buy, 350), 0u);
    CHECK_EQ(penny.outOfBandAdds(), 1u);
    CHECK_EQ(penny.bestBid(), kNoPrice);

    cfg.subDollarTickWire = 1;
    dut::BookBuilder subDollar(cfg);
    subDollar.apply(bytesOf(mkAdd(1u, 'B', 1u, 350)));
    CHECK_EQ(subDollar.tickWire(), 1);
    CHECK_EQ(subDollar.bandLow(), 350 - 10);
    CHECK_EQ(subDollar.bandHigh(), 350 + 10);
    subDollar.apply(bytesOf(mkAdd(2u, 'S', 1u, 351)));
    CHECK_EQ(subDollar.bestAsk(), 351);

    dut::BookBuilder junkFirst(cfg);
    junkFirst.apply(bytesOf(mkAdd(1u, 'S', 5u, 50000)));
    CHECK_EQ(junkFirst.tickWire(), 100);
    junkFirst.apply(bytesOf(mkAdd(2u, 'B', 100u, 5384)));
    CHECK_EQ(junkFirst.tickWire(), 100);
    CHECK_EQ(junkFirst.reanchors(), 0u);
    CHECK_EQ(junkFirst.bestBid(), kNoPrice);
    CHECK_EQ(junkFirst.outOfBandAdds(), 1u);
    CHECK_EQ(junkFirst.parkedShares(), 100u);
    itch::TradeNonCross flip{};
    flip.messageType = itch::MessageType::TradeNonCross;
    flip.shares      = 1;
    flip.price       = 5384;
    junkFirst.apply(bytesOf(flip));
    CHECK_EQ(junkFirst.tickWire(), 1);
    CHECK_EQ(junkFirst.reanchors(), 1u);
    CHECK_EQ(junkFirst.bestBid(), 5384);
    CHECK_EQ(junkFirst.restingShares(1u), 5u);
    junkFirst.apply(bytesOf(mkAdd(3u, 'S', 100u, 5385)));
    junkFirst.apply(bytesOf(mkAdd(4u, 'B', 100u, 5380)));
    CHECK_EQ(junkFirst.bestAsk(), 5385);
    junkFirst.apply(bytesOf(mkDelete(2u)));
    CHECK_EQ(junkFirst.bestBid(), 5380);
    CHECK_EQ(junkFirst.liveOrders(), 3u);
}

void test_out_of_band_trade_reanchors_and_rebuilds() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 16;
    cfg.bandFraction = 0.0;
    dut::BookBuilder book(cfg);
    book.apply(bytesOf(mkAdd(1u, 'S', 10u, 99'990'000)));
    CHECK_EQ(book.bestAsk(), 99'990'000);
    book.apply(bytesOf(mkAdd(2u, 'B', 100u, 320'000)));
    book.apply(bytesOf(mkAdd(3u, 'S', 100u, 320'100)));
    CHECK_EQ(book.outOfBandAdds(), 2u);
    CHECK_EQ(book.bestBid(), kNoPrice);
    CHECK_EQ(book.liveOrders(), 3u);

    book.apply(bytesOf(mkExec(2u, 40u)));
    CHECK_EQ(book.reanchors(), 1u);
    CHECK_EQ(book.bandLow(), 320'000 - 1600);
    CHECK_EQ(book.bestBid(), 320'000);
    CHECK_EQ(book.bestAsk(), 320'100);
    CHECK_EQ(book.sizeAt(Side::Buy, 320'000), 60u);
    CHECK_EQ(book.sizeAt(Side::Sell, 320'100), 100u);
    CHECK_EQ(book.restingShares(1u), 10u);
    CHECK_EQ(book.liveOrders(), 3u);

    itch::TradeNonCross p{};
    p.messageType = itch::MessageType::TradeNonCross;
    p.shares      = 5;
    p.price       = 320'050;
    book.apply(bytesOf(p));
    CHECK_EQ(book.reanchors(), 1u);
    p.price = 400'000;
    book.apply(bytesOf(p));
    CHECK_EQ(book.reanchors(), 2u);
    CHECK_EQ(book.bestBid(), kNoPrice);
    CHECK_EQ(book.bestAsk(), kNoPrice);
    CHECK_EQ(book.liveOrders(), 3u);
}

void test_band_shift_keeps_levels_and_parks_the_rest() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 16;
    cfg.bandFraction = 0.0;
    dut::BookBuilder book(cfg);
    book.apply(bytesOf(mkAdd(1u, 'B', 100u, 320'000)));
    book.apply(bytesOf(mkAdd(2u, 'S', 100u, 320'100)));
    book.apply(bytesOf(mkAdd(3u, 'B', 50u, 319'000)));
    CHECK_EQ(book.bandLow(), 320'000 - 1600);
    CHECK_EQ(book.bandHigh(), 320'000 + 1600);
    CHECK_EQ(book.parkedShares(), 0u);

    itch::TradeNonCross p{};
    p.messageType = itch::MessageType::TradeNonCross;
    p.shares      = 5;
    p.price       = 321'700;
    book.apply(bytesOf(p));
    CHECK_EQ(book.reanchors(), 1u);
    CHECK_EQ(book.bandLow(), 321'700 - 1600);
    CHECK_EQ(book.bandHigh(), 321'700 + 1600);
    CHECK_EQ(book.bestAsk(), 320'100);
    CHECK_EQ(book.sizeAt(Side::Sell, 320'100), 100u);
    CHECK_EQ(book.bestBid(), kNoPrice);
    CHECK_EQ(book.parkedShares(), 150u);
    CHECK_EQ(book.liveOrders(), 3u);

    book.apply(bytesOf(mkDelete(3u)));
    CHECK_EQ(book.parkedShares(), 100u);
    CHECK_EQ(book.liveOrders(), 2u);

    book.apply(bytesOf(mkAdd(4u, 'B', 70u, 321'000)));
    CHECK_EQ(book.bestBid(), 321'000);
    CHECK_EQ(book.sizeAt(Side::Buy, 321'000), 70u);

    p.price = 319'500;
    book.apply(bytesOf(p));
    CHECK_EQ(book.reanchors(), 2u);
    CHECK_EQ(book.bestBid(), 321'000);
    CHECK_EQ(book.sizeAt(Side::Buy, 320'000), 100u);
    CHECK_EQ(book.sizeAt(Side::Buy, 321'000), 70u);
    CHECK_EQ(book.bestAsk(), 320'100);
    CHECK_EQ(book.sizeAt(Side::Sell, 320'100), 100u);
    CHECK_EQ(book.parkedShares(), 0u);

    p.price = 400'000;
    book.apply(bytesOf(p));
    CHECK_EQ(book.reanchors(), 3u);
    CHECK_EQ(book.bestBid(), kNoPrice);
    CHECK_EQ(book.bestAsk(), kNoPrice);
    CHECK_EQ(book.parkedShares(), 270u);
    book.apply(bytesOf(mkAdd(5u, 'S', 10u, 400'100)));
    CHECK_EQ(book.bestAsk(), 400'100);
}

void test_profile_anchor_builds_band_before_any_message() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 16;
    cfg.bandFraction = 0.10;
    cfg.anchorPrice  = 3'200'000;
    dut::BookBuilder book(cfg);
    CHECK(book.anchored());
    CHECK_EQ(book.reanchors(), 0u);
    CHECK_EQ(book.bandLow(), 3'200'000 - 320'000);
    CHECK_EQ(book.bandHigh(), 3'200'000 + 320'000);
    book.apply(bytesOf(mkAdd(1u, 'B', 100u, 2'900'000)));
    CHECK_EQ(book.bestBid(), 2'900'000);
    CHECK_EQ(book.outOfBandAdds(), 0u);
    CHECK_EQ(book.reanchors(), 0u);
}

void test_best_rescan_across_gaps_and_words() {
    dut::BookConfig cfg{};
    cfg.tickWire     = 100;
    cfg.bandTicks    = 4096;
    cfg.bandFraction = 0.0;
    dut::BookBuilder book(cfg);
    book.apply(bytesOf(mkAdd(1u, 'B', 1u, 3'200'000)));
    book.apply(bytesOf(mkAdd(2u, 'B', 1u, 3'000'000)));
    book.apply(bytesOf(mkAdd(3u, 'B', 1u, 2'900'100)));
    book.apply(bytesOf(mkAdd(4u, 'S', 1u, 3'200'100)));
    book.apply(bytesOf(mkAdd(5u, 'S', 1u, 3'400'000)));
    book.apply(bytesOf(mkAdd(6u, 'S', 1u, 3'599'900)));
    CHECK_EQ(book.bestBid(), 3'200'000);
    CHECK_EQ(book.bestAsk(), 3'200'100);
    book.apply(bytesOf(mkDelete(1u)));
    CHECK_EQ(book.bestBid(), 3'000'000);
    book.apply(bytesOf(mkDelete(2u)));
    CHECK_EQ(book.bestBid(), 2'900'100);
    book.apply(bytesOf(mkDelete(3u)));
    CHECK_EQ(book.bestBid(), kNoPrice);
    book.apply(bytesOf(mkDelete(4u)));
    CHECK_EQ(book.bestAsk(), 3'400'000);
    book.apply(bytesOf(mkDelete(5u)));
    CHECK_EQ(book.bestAsk(), 3'599'900);
    book.apply(bytesOf(mkDelete(6u)));
    CHECK_EQ(book.bestAsk(), kNoPrice);
    book.apply(bytesOf(mkAdd(7u, 'B', 1u, 2'790'500)));
    book.apply(bytesOf(mkAdd(8u, 'B', 1u, 2'790'600)));
    CHECK_EQ(book.bestBid(), 2'790'600);
    book.apply(bytesOf(mkExec(8u, 1u)));
    CHECK_EQ(book.bestBid(), 2'790'500);
}

int main() {
    test_best_rescan_across_gaps_and_words();
    test_band_shift_keeps_levels_and_parks_the_rest();
    test_profile_anchor_builds_band_before_any_message();
    test_dynamic_band_anchors_on_first_add();
    test_dynamic_band_scales_with_price_and_clamps_at_zero();
    test_out_of_band_trade_reanchors_and_rebuilds();
    test_book();
    test_own_orders_excluded_from_view();
    return abt::test::summary("dutbook");
}
