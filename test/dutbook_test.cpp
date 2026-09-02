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

int main() {
    test_book();
    test_own_orders_excluded_from_view();
    return abt::test::summary("dutbook");
}
