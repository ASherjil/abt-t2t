#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/sim/Venue.hpp"

using namespace abt;

namespace {

struct RecSink {
    std::vector<std::vector<std::byte>> md;
    std::vector<std::vector<std::byte>> oe;

    void marketData(std::span<const std::byte> b) {
        md.emplace_back(b.begin(), b.end());
    }

    void orderEntry(std::span<const std::byte> b) {
        oe.emplace_back(b.begin(), b.end());
    }

    void clear() {
        md.clear();
        oe.clear();
    }
};

template <class M>
M decode(const std::vector<std::byte>& v) {
    M m{};
    std::memcpy(&m, v.data(), sizeof m);
    return m;
}

std::size_t countOe(const RecSink& s, char type) {
    std::size_t n = 0;
    for (const auto& m : s.oe) {
        if (static_cast<char>(m[0]) == type) {
            ++n;
        }
    }
    return n;
}

std::size_t countMd(const RecSink& s, char type) {
    std::size_t n = 0;
    for (const auto& m : s.md) {
        if (static_cast<char>(m[0]) == type) {
            ++n;
        }
    }
    return n;
}

ouch::EnterOrder enter(std::uint32_t user, char side, std::uint32_t qty, std::uint64_t price) {
    ouch::EnterOrder o{};
    o.type               = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum         = user;
    o.side               = side;
    o.quantity           = qty;
    o.symbol             = std::string_view{"AAPL"};
    o.price              = price;
    o.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    o.display            = static_cast<char>(ouch::Display::Visible);
    o.capacity           = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType          = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId            = std::string_view{"CID"};
    o.appendageLength    = 0;
    return o;
}

constexpr OrderId       kClientBase = 1ull << 62;
constexpr std::uint32_t kPx320      = 3'200'000;
constexpr std::uint32_t kPx321      = 3'210'000;
constexpr std::uint32_t kPx322      = 3'220'000;
constexpr std::uint32_t kPx319      = 3'190'000;

struct Fx {
    RecSink        sink;
    Venue<RecSink> v{sink, "AAPL", 13, 1, 100000, 100, kClientBase, 1u << 10};
};

void test_mirror_builds_book_silently() {
    Fx f;
    f.v.mirrorAdd(1001, Side::Buy, kPx320, 100, 1);
    f.v.mirrorAdd(1002, Side::Sell, kPx321, 200, 2);
    f.v.mirrorAdd(1003, Side::Sell, kPx321, 50, 3);
    CHECK_EQ(f.sink.md.size(), 0u);
    CHECK_EQ(f.v.bestBid(), 32000);
    CHECK_EQ(f.v.bestAsk(), 32100);
    CHECK_EQ(f.v.liveOrders(), 3u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 250u);

    f.v.mirrorExecute(1002, 150, 4);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 100u);
    f.v.mirrorCancel(1003, 20, 5);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 80u);
    f.v.mirrorReplace(1001, 1004, 300, kPx319, 6);
    CHECK_EQ(f.v.bestBid(), 31900);
    CHECK_EQ(f.v.book().volumeAt(Side::Buy, 31900), 300u);
    f.v.mirrorDelete(1004, 7);
    CHECK(f.v.bestBid() == kNoPrice);
    CHECK_EQ(f.sink.md.size(), 0u);
    CHECK_EQ(f.sink.oe.size(), 0u);

    f.v.mirrorDelete(4242, 8);
    f.v.mirrorExecute(1002, 500, 9);
    const MirrorStats& s = f.v.mirrorStats();
    CHECK_EQ(s.adds, 3u);
    CHECK_EQ(s.executes, 2u);
    CHECK_EQ(s.cancels, 1u);
    CHECK_EQ(s.deletes, 2u);
    CHECK_EQ(s.replaces, 1u);
    CHECK_EQ(s.unknownRef, 1u);
    CHECK_EQ(s.overReduce, 1u);
    CHECK_EQ(s.shadowFills, 0u);
}

void test_real_orders_never_match_each_other() {
    Fx f;
    f.v.mirrorAdd(1, Side::Buy, kPx322, 100, 1);
    f.v.mirrorAdd(2, Side::Sell, kPx320, 100, 2);
    CHECK_EQ(f.v.liveOrders(), 2u);
    CHECK_EQ(f.v.bestBid(), 32200);
    CHECK_EQ(f.v.bestAsk(), 32000);
    CHECK_EQ(f.sink.md.size(), 0u);
}

void test_real_add_crossing_client_quote_fills_client() {
    Fx f;
    f.v.mirrorAdd(1, Side::Sell, kPx322, 100, 1);
    f.v.onEnterOrder(enter(7, 'S', 100, kPx321), 2);
    f.sink.clear();

    f.v.mirrorAdd(2, Side::Buy, kPx321, 60, 3);
    CHECK_EQ(countOe(f.sink, 'E'), 1u);
    CHECK_EQ(countMd(f.sink, 'E'), 1u);
    const auto e = decode<itch::OrderExecuted>(f.sink.md.back());
    CHECK_EQ(e.orderRef.value(), kClientBase);
    CHECK_EQ(e.executedShares.value(), 60u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 40u);
    CHECK_EQ(f.v.book().volumeAt(Side::Buy, 32100), 0u);
    CHECK_EQ(f.v.mirrorStats().crossFills, 1u);

    f.v.mirrorAdd(3, Side::Buy, kPx322, 100, 4);
    CHECK_EQ(countOe(f.sink, 'E'), 2u);
    CHECK_EQ(f.v.book().volumeAt(Side::Buy, 32200), 60u);
    CHECK_EQ(f.v.clientOrders(), 0u);
}

void test_shadow_fill_priority() {
    Fx f;
    f.v.mirrorAdd(10, Side::Sell, kPx321, 100, 1);
    f.v.onEnterOrder(enter(1, 'S', 50, kPx321), 2);
    f.v.mirrorAdd(11, Side::Sell, kPx321, 100, 3);
    f.sink.clear();

    f.v.mirrorExecute(10, 30, 4);
    CHECK_EQ(countOe(f.sink, 'E'), 0u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 220u);

    f.v.mirrorExecute(11, 30, 5);
    CHECK_EQ(countOe(f.sink, 'E'), 1u);
    const auto ex = decode<ouch::Executed>(f.sink.oe.back());
    CHECK_EQ(ex.quantity.value(), 30u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 160u);
    CHECK_EQ(f.v.mirrorStats().shadowFills, 1u);
    CHECK_EQ(f.v.mirrorStats().shadowShares, 30u);

    f.v.mirrorAdd(12, Side::Sell, kPx322, 100, 6);
    f.sink.clear();
    f.v.mirrorExecute(12, 100, 7);
    CHECK_EQ(countOe(f.sink, 'E'), 1u);
    const auto ex2 = decode<ouch::Executed>(f.sink.oe.back());
    CHECK_EQ(ex2.quantity.value(), 20u);
    CHECK_EQ(f.v.clientOrders(), 0u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 140u);
}

void test_shadow_fill_ignores_other_side() {
    Fx f;
    f.v.mirrorAdd(10, Side::Buy, kPx320, 100, 1);
    f.v.onEnterOrder(enter(1, 'S', 50, kPx321), 2);
    f.sink.clear();
    f.v.mirrorExecute(10, 100, 3);
    CHECK_EQ(countOe(f.sink, 'E'), 0u);
    CHECK_EQ(f.v.clientOrders(), 1u);
}

void test_client_aggression_counts_impact() {
    Fx f;
    f.v.mirrorAdd(10, Side::Sell, kPx321, 100, 1);
    f.v.onEnterOrder(enter(1, 'B', 40, kPx321), 2);
    CHECK_EQ(f.v.mirrorStats().impactFills, 1u);
    CHECK_EQ(f.v.book().volumeAt(Side::Sell, 32100), 60u);
    CHECK_EQ(countMd(f.sink, 'E'), 1u);
    const auto e = decode<itch::OrderExecuted>(f.sink.md.back());
    CHECK_EQ(e.orderRef.value(), 10u);
    f.v.mirrorExecute(10, 100, 3);
    CHECK_EQ(f.v.mirrorStats().overReduce, 1u);
    CHECK_EQ(f.v.liveOrders(), 0u);
}

void test_reset_day_cancels_clients_and_clears() {
    Fx f;
    f.v.mirrorAdd(10, Side::Sell, kPx321, 100, 1);
    f.v.onEnterOrder(enter(1, 'B', 40, kPx320), 2);
    f.v.onEnterOrder(enter(2, 'S', 40, kPx322), 3);
    f.sink.clear();
    f.v.resetDay(4);
    CHECK_EQ(countOe(f.sink, 'C'), 2u);
    CHECK_EQ(countMd(f.sink, 'D'), 2u);
    CHECK_EQ(f.v.liveOrders(), 0u);
    CHECK_EQ(f.v.clientOrders(), 0u);
    CHECK(f.v.book().empty());
    f.v.mirrorAdd(10, Side::Sell, kPx321, 100, 5);
    CHECK_EQ(f.v.liveOrders(), 1u);
    CHECK_EQ(f.v.mirrorStats().unknownRef, 0u);
}

}   // namespace

int main() {
    test_mirror_builds_book_silently();
    test_real_orders_never_match_each_other();
    test_real_add_crossing_client_quote_fills_client();
    test_shadow_fill_priority();
    test_shadow_fill_ignores_other_side();
    test_client_aggression_counts_impact();
    test_reset_day_cancels_clients_and_clears();
    return abt::test::summary("mirror_test");
}
