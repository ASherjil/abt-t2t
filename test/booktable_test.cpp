#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "t2t/dut/BookTable.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/replay/FeedValidator.hpp"
#include "t2t/util/HugePageArena.hpp"

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

itch::OrderDelete mkDelete(std::uint16_t locate, OrderId ref) {
    itch::OrderDelete d{};
    d.messageType = itch::MessageType::OrderDelete;
    d.stockLocate = locate;
    d.orderRef    = ref;
    return d;
}

itch::OrderExecuted mkExec(std::uint16_t locate, OrderId ref, Quantity shares) {
    itch::OrderExecuted e{};
    e.messageType    = itch::MessageType::OrderExecuted;
    e.stockLocate    = locate;
    e.orderRef       = ref;
    e.executedShares = shares;
    return e;
}

itch::StockTradingAction mkHalt(std::uint16_t locate, char state) {
    itch::StockTradingAction h{};
    h.messageType  = itch::MessageType::StockTradingAction;
    h.stockLocate  = locate;
    h.tradingState = static_cast<itch::TradingState>(state);
    return h;
}

dut::BookTableConfig baseCfg() {
    dut::BookTableConfig cfg{};
    cfg.tickWire      = 100;
    cfg.coldBandTicks = 64;
    cfg.hotBandTicks  = 256;
    cfg.bandFraction  = 0.0;
    cfg.coldMapSlots  = 64;
    cfg.hotMapSlots   = 1024;
    cfg.hotSymbols    = {"AAPL", "MSFT"};
    return cfg;
}

void test_directory_resolves_hot_symbols() {
    dut::BookTable table(baseCfg());
    CHECK_EQ(table.hotCount(), 2u);
    CHECK(!table.hot(0).resolved);

    CHECK_EQ(table.apply(bytesOf(mkDir(13, "AAPL"))), 0);
    CHECK_EQ(table.apply(bytesOf(mkDir(500, "MSFT"))), 1);
    CHECK_EQ(table.apply(bytesOf(mkDir(7, "ZZZZ"))), dut::BookTable::kCold);
    CHECK(table.hot(0).resolved && table.hot(0).locate == 13);
    CHECK(table.hot(1).resolved && table.hot(1).locate == 500);
    CHECK_EQ(table.symbols(), 3u);
    CHECK_EQ(table.hotIndexOf(13), 0);
    CHECK_EQ(table.hotIndexOf(7), dut::BookTable::kCold);
    CHECK_EQ(table.hotBook(0).orderCapacity(), 1024u);
    CHECK_EQ(table.book(7)->orderCapacity(), 64u);

    CHECK_EQ(table.apply(bytesOf(mkAdd(13, 1u, 'B', 100u, 32000))), 0);
    CHECK_EQ(table.apply(bytesOf(mkAdd(7, 2u, 'S', 50u, 900))), dut::BookTable::kCold);
    CHECK_EQ(table.hotBook(0).bestBid(), 32000);
    CHECK_EQ(table.book(7)->bestAsk(), 900);
    CHECK_EQ(table.hotBook(1).bestBid(), kNoPrice);
    CHECK_EQ(table.liveOrders(), 2u);
}

void test_undirected_locate_gets_a_cold_book() {
    dut::BookTable table(baseCfg());
    CHECK_EQ(table.apply(bytesOf(mkAdd(99, 1u, 'B', 10u, 1000))), dut::BookTable::kCold);
    CHECK_EQ(table.undirected(), 1u);
    CHECK_EQ(table.symbols(), 1u);
    CHECK(table.book(99) != nullptr && table.book(99)->bestBid() == 1000);
    CHECK_EQ(table.apply(bytesOf(mkDelete(99, 1u))), dut::BookTable::kCold);
    CHECK_EQ(table.undirected(), 1u);
    CHECK_EQ(table.book(99)->liveOrders(), 0u);
}

void test_halt_and_clear() {
    dut::BookTable table(baseCfg());
    (void)table.apply(bytesOf(mkDir(13, "AAPL")));
    (void)table.apply(bytesOf(mkAdd(13, 1u, 'B', 100u, 32000)));
    CHECK(table.hot(0).trading);
    CHECK_EQ(table.apply(bytesOf(mkHalt(13, 'H'))), 0);
    CHECK(!table.hot(0).trading);
    CHECK_EQ(table.apply(bytesOf(mkHalt(13, 'T'))), 0);
    CHECK(table.hot(0).trading);
    (void)table.apply(bytesOf(mkHalt(13, 'H')));
    table.clearAll();
    CHECK(table.hot(0).trading);
    CHECK_EQ(table.hotBook(0).liveOrders(), 0u);
    CHECK(table.hotBook(0).anchored());
    (void)table.apply(bytesOf(mkDir(13, "AAPL")));
    CHECK_EQ(table.symbols(), 1u);
}

void test_table_on_arena() {
    util::HugePageArena  arena(8u << 20);
    dut::BookTableConfig cfg = baseCfg();
    cfg.memory               = arena.resource();
    dut::BookTable table(cfg);
    for (std::uint16_t l = 1; l <= 200; ++l) {
        (void)table.apply(bytesOf(mkAdd(l, l, 'B', 1u, 1000 + l * 100)));
    }
    CHECK_EQ(table.symbols(), 200u);
    CHECK_EQ(table.book(150)->bestBid(), 1000 + 150 * 100);
}

itch::SystemEvent mkSys(char code) {
    itch::SystemEvent s{};
    s.messageType = itch::MessageType::SystemEvent;
    s.eventCode   = static_cast<itch::SystemEventCode>(code);
    return s;
}

itch::StockTradingAction mkHaltAt(std::uint16_t locate, char state, std::uint64_t ns) {
    itch::StockTradingAction h = mkHalt(locate, state);
    h.timestamp                = ns;
    return h;
}

itch::AddOrder mkAddAt(std::uint16_t locate, OrderId ref, char side, Quantity shares, Price price,
                       std::uint64_t ns) {
    itch::AddOrder a = mkAdd(locate, ref, side, shares, price);
    a.timestamp      = ns;
    return a;
}

void test_feed_validator_resume_grace() {
    replay::FeedValidator v(baseCfg());
    v.onMessage(bytesOf(mkSys('Q')));
    v.onMessage(bytesOf(mkDir(13, "AAPL")));
    v.onMessage(bytesOf(mkHaltAt(13, 'P', 1'000'000'000ull)));
    v.onMessage(bytesOf(mkAddAt(13, 1u, 'S', 100u, 32000, 1'000'000'100ull)));
    v.onMessage(bytesOf(mkAddAt(13, 2u, 'B', 100u, 32500, 1'000'000'200ull)));
    v.onMessage(bytesOf(mkHaltAt(13, 'T', 2'000'000'000ull)));
    v.onMessage(bytesOf(mkAddAt(13, 3u, 'B', 5u, 32600, 2'000'500'000ull)));
    v.onMessage(bytesOf(mkAddAt(13, 4u, 'B', 5u, 32700, 2'020'000'000ull)));
    const replay::FeedTotals t = v.totals();
    CHECK_EQ(t.resumeXing, 1u);
    CHECK_EQ(t.crossed, 1u);
    CHECK_EQ(t.locked, 0u);
}

void test_feed_validator_counts_faults() {
    replay::FeedValidator v(baseCfg());
    v.onMessage(bytesOf(mkSys('Q')));
    v.onMessage(bytesOf(mkDir(13, "AAPL")));
    v.onMessage(bytesOf(mkAdd(13, 1u, 'B', 100u, 32000)));
    v.onMessage(bytesOf(mkAdd(13, 2u, 'S', 100u, 32100)));
    v.onMessage(bytesOf(mkExec(13, 1u, 40u)));
    v.onMessage(bytesOf(mkExec(13, 1u, 80u)));
    v.onMessage(bytesOf(mkDelete(13, 77u)));
    v.onMessage(bytesOf(mkAdd(13, 3u, 'B', 5u, 32200)));
    v.onMessage(bytesOf(mkAdd(42, 4u, 'B', 5u, 500)));

    const replay::FeedTotals t = v.totals();
    CHECK_EQ(t.symbols, 2u);
    CHECK_EQ(t.messages, 8u);
    CHECK_EQ(t.unknownRef, 1u);
    CHECK_EQ(t.overReduce, 1u);
    CHECK_EQ(t.crossed, 1u);
    const replay::SymbolStats& s = v.perSymbol()[13];
    CHECK(s.name == "AAPL");
    CHECK_EQ(s.messages, 7u);
    CHECK_EQ(s.maxLive, 2u);
    CHECK_EQ(v.perSymbol()[42].messages, 1u);
}

}   // namespace

int main() {
    test_directory_resolves_hot_symbols();
    test_undirected_locate_gets_a_cold_book();
    test_halt_and_clear();
    test_table_on_arena();
    test_feed_validator_counts_faults();
    test_feed_validator_resume_grace();
    return abt::test::summary("booktable");
}
