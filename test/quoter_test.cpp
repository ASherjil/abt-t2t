//
// Unit test for the market-making quoter (abt::dut::QuoterStrategy): micro-price fair value,
// two-sided spread, inventory skew, and the pull-quotes case.
//

#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/QuoterStrategy.hpp"
#include "abt/protocol/Itch50.hpp"

using namespace abt;

namespace {

template <class T>
std::span<const std::byte> bytesOf(const T& msg) {
    return {reinterpret_cast<const std::byte*>(&msg), sizeof msg};
}

void addOrder(dut::BookBuilder& book, OrderId ref, char side, Quantity shares, Price price) {
    itch::AddOrder a{};
    a.messageType = 'A';
    a.orderRef    = ref;
    a.side        = side;
    a.shares      = shares;
    a.price       = static_cast<std::uint32_t>(price);
    book.apply(bytesOf(a));
}

dut::QuoterConfig baseCfg() {
    dut::QuoterConfig cfg{};
    cfg.tickWire         = 1;
    cfg.halfSpreadTicks  = 5;
    cfg.quoteQty         = 100;
    cfg.skewTicksPerUnit = 0.0;
    cfg.minPrice         = 0;
    cfg.maxPrice         = 100000;
    return cfg;
}

void test_symmetric() {
    dut::BookBuilder book(0, 100000, 1);
    addOrder(book, 1, 'B', 100, 10000);
    addOrder(book, 2, 'S', 100, 10100);

    dut::QuoterStrategy     q(baseCfg());
    const dut::QuoteTargets t = q.onBook(book, dut::Account{0});

    // Equal sizes -> fair = mid = 10050; +/- 5 tick half-spread.
    CHECK(t.quoteBid);
    CHECK(t.quoteAsk);
    CHECK_EQ(t.bidPrice, 10045);
    CHECK_EQ(t.askPrice, 10055);
    CHECK_EQ(t.bidQty, 100u);
    CHECK_EQ(t.askQty, 100u);
}

void test_imbalance_lifts_fair() {
    dut::BookBuilder book(0, 100000, 1);
    addOrder(book, 1, 'B', 300, 10000);   // heavier bid -> micro-price leans up
    addOrder(book, 2, 'S', 100, 10100);

    dut::QuoterStrategy     q(baseCfg());
    const dut::QuoteTargets t = q.onBook(book, dut::Account{0});

    // micro = (10000*100 + 10100*300)/400 = 10075; quotes 10070 / 10080.
    CHECK_EQ(t.bidPrice, 10070);
    CHECK_EQ(t.askPrice, 10080);
}

void test_inventory_skew() {
    dut::BookBuilder book(0, 100000, 1);
    addOrder(book, 1, 'B', 100, 10000);
    addOrder(book, 2, 'S', 100, 10100);

    dut::QuoterConfig cfg = baseCfg();
    cfg.skewTicksPerUnit  = 0.001;   // 1000 shares -> 1 tick of skew
    dut::QuoterStrategy q(cfg);

    const dut::QuoteTargets longT = q.onBook(book, dut::Account{1000});   // long -> shift down 1
    CHECK_EQ(longT.bidPrice, 10044);
    CHECK_EQ(longT.askPrice, 10054);

    const dut::QuoteTargets shortT = q.onBook(book, dut::Account{-1000});   // short -> shift up 1
    CHECK_EQ(shortT.bidPrice, 10046);
    CHECK_EQ(shortT.askPrice, 10056);
}

void test_no_market_pulls_quotes() {
    dut::BookBuilder book(0, 100000, 1);
    addOrder(book, 1, 'B', 100, 10000);   // one-sided: no ask

    dut::QuoterStrategy     q(baseCfg());
    const dut::QuoteTargets t = q.onBook(book, dut::Account{0});
    CHECK(!t.quoteBid);
    CHECK(!t.quoteAsk);
}

}   // namespace

int main() {
    test_symmetric();
    test_imbalance_lifts_fair();
    test_inventory_skew();
    test_no_market_pulls_quotes();
    return abt::test::summary("quoter");
}
