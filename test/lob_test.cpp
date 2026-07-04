//
// Matching-engine invariants: price-time priority, sweeps, cancels, best-quote upkeep.
//

#include <vector>

#include "abt/lob/OrderBook.hpp"
#include "TestHarness.hpp"

using namespace abt;
using lob::Side;

namespace {

struct Rec {
    std::vector<lob::Trade> trades;
    void onTrade(const lob::Trade& t) { trades.push_back(t); }
};

lob::Quantity filled(const Rec& r) {
    lob::Quantity q = 0;
    for (const auto& t : r.trades) q += t.qty;
    return q;
}

void test_rest_only_no_cross() {
    lob::OrderBook book(1, 1000);
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
    lob::OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    const lob::Handle rem = book.add(99, Side::Buy, 52, 100, sink);

    CHECK_EQ(sink.trades.size(), 1u);
    CHECK_EQ(sink.trades[0].restingId, 1u);
    CHECK_EQ(sink.trades[0].aggressorId, 99u);
    CHECK_EQ(sink.trades[0].price, 52);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK(sink.trades[0].restingFilled);
    CHECK(rem == lob::kNilHandle);
    CHECK_EQ(book.bestAsk(), lob::kNoPrice);
    CHECK(book.empty());
}

void test_price_time_priority() {
    lob::OrderBook book(1, 1000);
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
    lob::OrderBook book(1, 1000);
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
    lob::OrderBook book(1, 1000);
    Rec sink;
    book.add(1, Side::Sell, 52, 100, sink);
    const lob::Handle rem = book.add(99, Side::Buy, 52, 150, sink);

    CHECK_EQ(sink.trades.size(), 1u);
    CHECK_EQ(sink.trades[0].qty, 100u);
    CHECK(rem != lob::kNilHandle);
    CHECK_EQ(book.bestAsk(), lob::kNoPrice);
    CHECK_EQ(book.bestBid(), 52);
    CHECK_EQ(book.volumeAt(Side::Buy, 52), 50u);
    CHECK_EQ(book.order(rem).qty, 50u);
}

void test_cancel_and_best_update() {
    lob::OrderBook book(1, 1000);
    const lob::Handle h = book.add(1, Side::Buy, 50, 100);
    CHECK_EQ(book.bestBid(), 50);
    CHECK_EQ(book.cancel(h), 100u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 0u);
    CHECK_EQ(book.bestBid(), lob::kNoPrice);
    CHECK(book.empty());
    CHECK_EQ(book.cancel(h), 0u);
}

void test_cancel_middle_of_fifo() {
    lob::OrderBook book(1, 1000);
    book.add(1, Side::Sell, 50, 100);
    const lob::Handle hb = book.add(2, Side::Sell, 50, 100);
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
    lob::OrderBook book(1, 1000);
    const lob::Handle h = book.add(1, Side::Buy, 50, 100);
    CHECK_EQ(book.reduce(h, 60), 40u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 60u);
    CHECK_EQ(book.reduce(h, 80), 0u);
    CHECK_EQ(book.volumeAt(Side::Buy, 50), 60u);
    CHECK_EQ(book.reduce(h, 0), 60u);
    CHECK_EQ(book.bestBid(), lob::kNoPrice);
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
