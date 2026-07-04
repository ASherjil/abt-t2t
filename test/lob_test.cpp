//
// Matching-engine invariants: price-time priority, sweeps, cancels, best-quote upkeep.
//

#include <vector>

#include "abt/lob/OrderBook.hpp"
#include "TestHarness.hpp"

using namespace abt;
namespace {

struct Rec {
    std::vector<Trade> trades;
    void onTrade(const Trade& t) { trades.push_back(t); }
};

Quantity filled(const Rec& r) {
    Quantity q = 0;
    for (const auto& t : r.trades) q += t.qty;
    return q;
}

void test_rest_only_no_cross() {
    OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Buy, 50, 100, sink);
    book.add(2, Side::Buy, 49, 200, sink);
    book.add(3, Side::Sell, 52, 100, sink);

    CHECK_EQ(sink.trades.size(), 0u);
    CHECK_EQ(book.bestBid(), 50);
    CHECK_EQ(book.bestAsk(), 52);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 100u);
    CHECK_EQ(book.volumeAt(Side::Buy, 49), 200u);
    CHECK_EQ(book.volumeAt(Side::Sell, 52), 100u);
    CHECK(book.bestBid() < book.bestAsk());
}

void test_full_aggressor_fill() {
    OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    const Handle rem = book.add(99, Side::Buy, 52, 100, sink);

    CHECK_EQ(sink.trades.size(), 1u);
    CHECK_EQ(sink.trades[0].restingId, 1u);
    CHECK_EQ(sink.trades[0].aggressorId, 99u);
    CHECK_EQ(sink.trades[0].price, 52);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK(sink.trades[0].restingFilled);
    CHECK(rem == kNilHandle);
    CHECK_EQ(book.bestAsk(), kNoPrice);
    CHECK(book.empty());
}

void test_price_time_priority() {
    OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    book.add(2, Side::Sell, 52, 100, sink);
    book.add(99, Side::Buy, 52, 150, sink);

    CHECK_EQ(sink.trades.size(), 2u);
    CHECK_EQ(sink.trades[0].restingId, 1u);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK(sink.trades[0].restingFilled);
    CHECK_EQ(sink.trades[1].restingId, 2u);
    CHECK_EQ(sink.trades[1].qty, 50u);
    CHECK(!sink.trades[1].restingFilled);
    CHECK_EQ(book.volumeAt(Side::Sell, 52), 50u);
    CHECK_EQ(book.bestAsk(), 52);
}

void test_multilevel_sweep_price_improvement() {
    OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    book.add(2, Side::Sell, 53, 100, sink);
    book.add(99, Side::Buy, 53, 150, sink);

    CHECK_EQ(sink.trades.size(), 2u);
    CHECK_EQ(sink.trades[0].price, 52);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK_EQ(sink.trades[1].price, 53);
    CHECK_EQ(sink.trades[1].qty, 50u);
    CHECK_EQ(book.bestAsk(), 53);
    CHECK_EQ(book.volumeAt(Side::Sell, 53), 50u);
    CHECK_EQ(filled(sink), 150u);
}

void test_aggressor_partial_rests() {
    OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    const Handle rem = book.add(99, Side::Buy, 52, 150, sink);

    CHECK_EQ(sink.trades.size(), 1u);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK(rem != kNilHandle);
    CHECK_EQ(book.bestAsk(), kNoPrice);
    CHECK_EQ(book.bestBid(), 52);
    CHECK_EQ(book.volumeAt(Side::Buy, 52), 50u);
    CHECK_EQ(book.order(rem).qty, 50u);
}

void test_cancel_and_best_update() {
    OrderBook book(1, 1000);
    const Handle h = book.add(1, Side::Buy, 50, 100);
    CHECK_EQ(book.bestBid(), 50);
    CHECK_EQ(book.cancel(h), 100u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 0u);
    CHECK_EQ(book.bestBid(), kNoPrice);
    CHECK(book.empty());
    CHECK_EQ(book.cancel(h), 0u);
}

void test_cancel_middle_of_fifo() {
    OrderBook book(1, 1000);
    book.add(1, Side::Sell, 50, 100);
    const Handle hb = book.add(2, Side::Sell, 50, 100);
    book.add(3, Side::Sell, 50, 100);
    CHECK_EQ(book.cancel(hb), 100u);
    CHECK_EQ(book.volumeAt(Side::Sell, 50), 200u);

    Rec sink;
    book.add(99, Side::Buy, 50, 250, sink);
    CHECK_EQ(sink.trades.size(), 2u);
    CHECK_EQ(sink.trades[0].restingId, 1u);
    CHECK_EQ(sink.trades[1].restingId, 3u);
    CHECK_EQ(filled(sink), 200u);
}

void test_reduce() {
    OrderBook book(1, 1000);
    const Handle h = book.add(1, Side::Buy, 50, 100);
    CHECK_EQ(book.reduce(h, 60), 40u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 60u);
    CHECK_EQ(book.reduce(h, 80), 0u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 60u);
    CHECK_EQ(book.reduce(h, 0), 60u);
    CHECK_EQ(book.bestBid(), kNoPrice);
}

}

int main() {
    test_rest_only_no_cross();
    test_full_aggressor_fill();
    test_price_time_priority();
    test_multilevel_sweep_price_improvement();
    test_aggressor_partial_rests();
    test_cancel_and_best_update();
    test_cancel_middle_of_fifo();
    test_reduce();
    return abt::test::summary("lob_test");
}
