#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "t2t/dut/DutSession.hpp"
#include "t2t/dut/Strategy.hpp"
#include "t2t/dut/TxStamp.hpp"
#include "t2t/protocol/EthIpUdp.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/protocol/SoupBinTcp.hpp"

using namespace abt;

itch::SystemEvent mkSys(char code);

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

struct JoinBid {
    Quantity qty;

    [[nodiscard]] dut::QuoteTargets onBook(const dut::BookBuilder& book, const dut::Account&) const noexcept {
        dut::QuoteTargets t{};
        if (book.bestBid() != kNoPrice) {
            t.quoteBid = true;
            t.bidPrice = book.bestBid();
            t.bidQty   = qty;
        }
        return t;
    }
};

struct TakeOnce {
    Price    trigger;
    Quantity qty;
    bool     armed = true;

    dut::QuoteTargets onBook(const dut::BookBuilder& book, const dut::Account&) noexcept {
        dut::QuoteTargets t{};
        const Price       ask = book.bestAsk();
        if (ask == kNoPrice || ask > trigger || !armed) {
            return t;
        }
        armed      = false;
        t.quoteBid = true;
        t.bidPrice = ask;
        t.bidQty   = qty;
        return t;
    }
};

struct FakeStampSource {
    std::vector<dut::TxCompletion> queue;
    std::size_t                    idx = 0;

    dut::TxCompletion pollTxTimestamp() noexcept {
        if (idx < queue.size()) {
            return queue[idx++];
        }
        return dut::TxCompletion{.userRef = 0, .sec = 0, .nsec = 0, .status = 0};
    }
};

static_assert(dut::Strategy<JoinBid>);
static_assert(dut::Strategy<TakeOnce>);
static_assert(dut::TxStampSource<FakeStampSource>);

dut::DutConfig baseCfg() {
    dut::DutConfig cfg{};
    cfg.symbols       = {"ABT"};
    cfg.locates       = {0};
    cfg.tickWire      = 1;
    cfg.hotBandTicks  = 500;
    cfg.coldBandTicks = 500;
    cfg.bandFraction  = 0.0;
    cfg.firstUserRef  = 7;
    return cfg;
}

ouch::Accepted accepted(std::uint32_t ref, Quantity qty) {
    ouch::Accepted a{};
    a.type       = ouch::OutType::Accepted;
    a.userRefNum = ref;
    a.quantity   = qty;
    a.orderState = ouch::OrderState::Live;
    return a;
}

void test_quote_lifecycle_through_session() {
    mold::Packer                packer("SESSION01", 1);
    std::array<std::byte, 2048> buf{};

    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(baseCfg(), JoinBid{10u});

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    sess.onMarketData(packer.finalize(), 111u);

    CHECK_EQ(sess.book().bestBid(), 100);
    CHECK_EQ(sess.book().bestAsk(), 102);
    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.capturedOrders().size(), 1u);
    ouch::EnterOrder o{};
    std::memcpy(&o, sess.capturedOrders()[0].data(), sizeof o);
    CHECK_EQ(o.type, ouch::InType::EnterOrder);
    CHECK_EQ(o.side, ouch::Side::Buy);
    CHECK_EQ(o.userRefNum.value(), 7u);
    CHECK_EQ(o.quantity.value(), 10u);
    CHECK_EQ(o.price.value(), 100u);
    CHECK(o.symbol.view() == std::string_view{"ABT"});
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::PendingNew);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(3u, 'B', 200u, 101)));
    sess.onMarketData(packer.finalize(), 222u);
    CHECK_EQ(sess.book().bestBid(), 101);
    CHECK_EQ(sess.ordersSent(), 1u);

    sess.onAck(bytesOf(accepted(7u, 10u)));
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::Live);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(4u, 'S', 50u, 103)));
    sess.onMarketData(packer.finalize(), 333u);
    CHECK_EQ(sess.ordersSent(), 2u);
    ouch::ReplaceOrder u{};
    std::memcpy(&u, sess.capturedOrders()[1].data(), sizeof u);
    CHECK_EQ(u.type, ouch::InType::ReplaceOrder);
    CHECK_EQ(u.origUserRefNum.value(), 7u);
    CHECK_EQ(u.userRefNum.value(), 8u);
    CHECK_EQ(u.price.value(), 101u);
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::PendingReplace);

    CHECK_EQ(sess.feed().gaps(), 0u);
}

void test_take_and_t2t() {
    dut::DutConfig cfg = baseCfg();
    cfg.firstUserRef   = 1;
    dut::DutSession<dut::IoMode::Loopback, TakeOnce> sess(cfg, TakeOnce{.trigger = 101, .qty = 5u});

    mold::Packer                packer("SESSION01", 1);
    std::array<std::byte, 2048> buf{};
    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    sess.onMarketData(packer.finalize(), 500u);
    CHECK_EQ(sess.ordersSent(), 0u);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(3u, 'S', 200u, 101)));
    sess.onMarketData(packer.finalize(), 1000u);
    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.book().bestAsk(), 101);

    ouch::EnterOrder o{};
    std::memcpy(&o, sess.capturedOrders()[0].data(), sizeof o);
    CHECK_EQ(o.side, ouch::Side::Buy);
    CHECK_EQ(o.price.value(), 101u);
    CHECK_EQ(o.quantity.value(), 5u);

    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 0);

    FakeStampSource src{};
    src.queue.push_back(dut::TxCompletion{.userRef = 1u, .sec = 0u, .nsec = 1850u, .status = 1u});
    sess.pollTxCompletions(src);
    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 1);
    CHECK_EQ(sess.t2t().min(), 850);
    CHECK_EQ(sess.t2t().percentile(50.0), 850);

    FakeStampSource stale{};
    stale.queue.push_back(dut::TxCompletion{.userRef = 999u, .sec = 0u, .nsec = 5000u, .status = 1u});
    sess.pollTxCompletions(stale);
    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 1);
}

void test_sequence_gap_and_stale() {
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(baseCfg(), JoinBid{10u});
    std::array<std::byte, 2048>                     buf{};

    mold::Packer p1("SESSION01", 1);
    p1.reset(buf.data(), buf.size());
    (void)p1.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    sess.onMarketData(p1.finalize(), 1u);
    CHECK_EQ(sess.feed().expected(), 2u);

    mold::Packer p5("SESSION01", 5);
    p5.reset(buf.data(), buf.size());
    (void)p5.append(bytesOf(mkAdd(2u, 'B', 100u, 99)));
    sess.onMarketData(p5.finalize(), 2u);
    CHECK_EQ(sess.feed().gaps(), 1u);
    CHECK_EQ(sess.feed().missed(), 3u);
    CHECK(!sess.feedValid());
    CHECK(!sess.tradingAllowed());
    CHECK_EQ(sess.feedFaults(), 1u);
    CHECK_EQ(sess.lastFaultSeq(), 5u);
    CHECK_EQ(sess.book().liveOrders(), 0u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 99), 0u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 100), 0u);

    mold::Packer p3("SESSION01", 3);
    p3.reset(buf.data(), buf.size());
    (void)p3.append(bytesOf(mkAdd(3u, 'B', 777u, 98)));
    sess.onMarketData(p3.finalize(), 3u);
    CHECK_EQ(sess.feed().stale(), 1u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 98), 0u);
    CHECK_EQ(sess.feed().expected(), 6u);

    mold::Packer p6("SESSION01", 6);
    p6.reset(buf.data(), buf.size());
    (void)p6.append(bytesOf(mkAdd(4u, 'B', 50u, 97)));
    sess.onMarketData(p6.finalize(), 4u);
    CHECK(!sess.feedValid());
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 97), 0u);

    mold::Packer p7("SESSION01", 7);
    p7.reset(buf.data(), buf.size());
    (void)p7.append(bytesOf(mkSys('O')));
    (void)p7.append(bytesOf(mkAdd(5u, 'B', 60u, 96)));
    sess.onMarketData(p7.finalize(), 5u);
    CHECK(sess.feedValid());
    CHECK(sess.tradingAllowed());
    CHECK_EQ(sess.feedFaults(), 1u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 96), 60u);
}

void test_gap_pulls_live_quote() {
    mold::Packer                                    packer("SESSION01", 1);
    std::array<std::byte, 2048>                     buf{};
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(baseCfg(), JoinBid{10u});

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    sess.onMarketData(packer.finalize(), 1u);
    CHECK_EQ(sess.ordersSent(), 1u);
    sess.onAck(bytesOf(accepted(7u, 10u)));
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::Live);

    mold::Packer p9("SESSION01", 9);
    p9.reset(buf.data(), buf.size());
    (void)p9.append(bytesOf(mkAdd(3u, 'B', 100u, 101)));
    sess.onMarketData(p9.finalize(), 2u);
    CHECK(!sess.feedValid());
    CHECK_EQ(sess.ordersSent(), 2u);
    ouch::CancelOrder c{};
    std::memcpy(&c, sess.capturedOrders()[1].data(), sizeof c);
    CHECK_EQ(c.type, ouch::InType::CancelOrder);
    CHECK_EQ(c.userRefNum.value(), 7u);
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::PendingCancel);
}

}   // namespace

itch::AddOrder mkAddAt(std::uint16_t locate, OrderId ref, char side, Quantity shares, Price price) {
    itch::AddOrder a = mkAdd(ref, side, shares, price);
    a.stockLocate    = locate;
    return a;
}

itch::SystemEvent mkSys(char code) {
    itch::SystemEvent s{};
    s.messageType = itch::MessageType::SystemEvent;
    s.eventCode   = static_cast<itch::SystemEventCode>(code);
    return s;
}

itch::StockTradingAction mkHalt(std::uint16_t locate, char state) {
    itch::StockTradingAction h{};
    h.messageType  = itch::MessageType::StockTradingAction;
    h.stockLocate  = locate;
    h.tradingState = static_cast<itch::TradingState>(state);
    return h;
}

void test_locate_filter_session_gating_and_reset() {
    mold::Packer                packer("SESSION01", 1);
    std::array<std::byte, 2048> buf{};
    dut::DutConfig              cfg = baseCfg();
    cfg.locates                     = {13};
    cfg.marketHoursOnly             = true;
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(cfg, JoinBid{10u});

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkSys('O')));
    (void)packer.append(bytesOf(mkAddAt(7, 1u, 'B', 500u, 900)));
    (void)packer.append(bytesOf(mkAddAt(13, 2u, 'B', 500u, 100)));
    sess.onMarketData(packer.finalize(), 1u);
    CHECK_EQ(sess.book().bestBid(), 100);
    CHECK_EQ(sess.foreignMessages(), 1u);
    CHECK_EQ(sess.sessionResets(), 1u);
    CHECK(!sess.tradingAllowed());
    CHECK_EQ(sess.ordersSent(), 0u);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkSys('Q')));
    (void)packer.append(bytesOf(mkAddAt(13, 3u, 'S', 100u, 102)));
    sess.onMarketData(packer.finalize(), 2u);
    CHECK(sess.tradingAllowed());
    CHECK_EQ(sess.ordersSent(), 1u);
    sess.onAck(bytesOf(accepted(7u, 10u)));

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkHalt(13, 'H')));
    sess.onMarketData(packer.finalize(), 3u);
    CHECK(!sess.tradingAllowed());
    CHECK_EQ(sess.ordersSent(), 2u);
    ouch::CancelOrder x{};
    std::memcpy(&x, sess.capturedOrders()[1].data(), sizeof x);
    CHECK_EQ(x.type, ouch::InType::CancelOrder);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkHalt(7, 'T')));
    sess.onMarketData(packer.finalize(), 4u);
    CHECK(!sess.tradingAllowed());
    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkHalt(13, 'T')));
    sess.onMarketData(packer.finalize(), 5u);
    CHECK(sess.tradingAllowed());

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkSys('M')));
    sess.onMarketData(packer.finalize(), 6u);
    CHECK(!sess.tradingAllowed());

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkSys('O')));
    sess.onMarketData(packer.finalize(), 7u);
    CHECK(sess.book().bestBid() == kNoPrice);
    CHECK_EQ(sess.book().liveOrders(), 0u);
    CHECK_EQ(sess.sessionResets(), 2u);
}

itch::StockDirectory mkDir(std::uint16_t locate, std::string_view name) {
    itch::StockDirectory r{};
    r.messageType = itch::MessageType::StockDirectory;
    r.stockLocate = locate;
    r.stock       = name;
    return r;
}

ouch::Executed executed(std::uint32_t ref, Quantity qty) {
    ouch::Executed e{};
    e.type       = ouch::OutType::Executed;
    e.userRefNum = ref;
    e.quantity   = qty;
    return e;
}

void test_two_hot_symbols_resolved_from_directory() {
    mold::Packer                packer("SESSION01", 1);
    std::array<std::byte, 2048> buf{};
    dut::DutConfig              cfg = baseCfg();
    cfg.symbols                     = {"ABT", "XYZ"};
    cfg.locates                     = {};
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(cfg, JoinBid{10u});

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkDir(13, "ABT")));
    (void)packer.append(bytesOf(mkDir(21, "XYZ")));
    (void)packer.append(bytesOf(mkDir(99, "COLD")));
    (void)packer.append(bytesOf(mkAddAt(13, 1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAddAt(13, 2u, 'S', 500u, 102)));
    (void)packer.append(bytesOf(mkAddAt(21, 3u, 'B', 500u, 200)));
    (void)packer.append(bytesOf(mkAddAt(21, 4u, 'S', 500u, 204)));
    (void)packer.append(bytesOf(mkAddAt(99, 5u, 'B', 500u, 300)));
    sess.onMarketData(packer.finalize(), 1u);

    CHECK_EQ(sess.books().symbols(), 3u);
    CHECK_EQ(sess.books().hot(0).locate, 13u);
    CHECK_EQ(sess.books().hot(1).locate, 21u);
    CHECK_EQ(sess.book(0).bestBid(), 100);
    CHECK_EQ(sess.book(1).bestBid(), 200);
    CHECK_EQ(sess.books().book(99)->bestBid(), 300);
    CHECK_EQ(sess.foreignMessages(), 0u);
    CHECK_EQ(sess.ordersSent(), 2u);

    ouch::EnterOrder a{};
    ouch::EnterOrder b{};
    std::memcpy(&a, sess.capturedOrders()[0].data(), sizeof a);
    std::memcpy(&b, sess.capturedOrders()[1].data(), sizeof b);
    CHECK(a.symbol.view() == std::string_view{"ABT"});
    CHECK(b.symbol.view() == std::string_view{"XYZ"});
    CHECK_EQ(a.price.value(), 100u);
    CHECK_EQ(b.price.value(), 200u);
    CHECK_EQ(a.userRefNum.value(), 7u);
    CHECK_EQ(b.userRefNum.value(), 8u);

    sess.onAck(bytesOf(accepted(7u, 10u)));
    sess.onAck(bytesOf(accepted(8u, 10u)));
    CHECK(sess.oms().slot(0, Side::Buy).state == dut::QuoteState::Live);
    CHECK(sess.oms().slot(1, Side::Buy).state == dut::QuoteState::Live);
    sess.onAck(bytesOf(executed(8u, 4u)));
    CHECK_EQ(sess.oms().account(0).position, 0);
    CHECK_EQ(sess.oms().account(1).position, 4);
    CHECK_EQ(sess.oms().netPosition(), 4);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAddAt(99, 6u, 'B', 500u, 301)));
    sess.onMarketData(packer.finalize(), 2u);
    CHECK_EQ(sess.ordersSent(), 2u);

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAddAt(21, 7u, 'B', 500u, 201)));
    sess.onMarketData(packer.finalize(), 3u);
    CHECK_EQ(sess.ordersSent(), 3u);
    ouch::ReplaceOrder u{};
    std::memcpy(&u, sess.capturedOrders()[2].data(), sizeof u);
    CHECK_EQ(u.type, ouch::InType::ReplaceOrder);
    CHECK_EQ(u.origUserRefNum.value(), 8u);
    CHECK_EQ(u.price.value(), 201u);
}

struct MockIo {
    std::vector<std::uint8_t>              tmpl;
    std::vector<std::uint8_t>              scratch;
    std::vector<std::vector<std::uint8_t>> frames;

    void prefillRing(std::span<const std::uint8_t> t) noexcept {
        tmpl.assign(t.begin(), t.end());
    }

    std::uint8_t* acquire(std::uint32_t n) noexcept {
        scratch.assign(n, 0);
        return scratch.data();
    }

    void commit() noexcept {
        frames.emplace_back(scratch);
    }

    bool send(std::span<const std::uint8_t> frame) noexcept {
        frames.emplace_back(frame.begin(), frame.end());
        return true;
    }

    std::vector<std::vector<std::uint8_t>> inbound;
    std::vector<std::uint8_t>              rxCur;
    std::size_t                            rxIdx = 0;

    RxFrame tryReceive() noexcept {
        if (rxIdx >= inbound.size()) {
            return RxFrame{.data = {}, .sec = 0, .nsec = 0, .status = 0};
        }
        rxCur = inbound[rxIdx];
        return RxFrame{.data = {rxCur.data(), rxCur.size()}, .sec = 0, .nsec = 0, .status = 1};
    }

    void release() noexcept {
        ++rxIdx;
    }
};

void test_cold_shard_routes_unquoted_symbols() {
    mold::Packer                packer("SESSION01", 1);
    std::array<std::byte, 2048> buf{};
    dut::DutConfig              cfg = baseCfg();
    cfg.symbols                     = {"ABT"};
    cfg.locates                     = {};
    cfg.coldShard                   = true;
    cfg.coldCore                    = -1;
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(cfg, JoinBid{10u});
    CHECK(sess.cold() != nullptr);
    sess.startCold();

    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkDir(13, "ABT")));
    (void)packer.append(bytesOf(mkDir(99, "COLD")));
    (void)packer.append(bytesOf(mkAddAt(13, 1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAddAt(13, 2u, 'S', 500u, 102)));
    (void)packer.append(bytesOf(mkAddAt(99, 3u, 'B', 500u, 300)));
    (void)packer.append(bytesOf(mkAddAt(98, 4u, 'S', 500u, 400)));
    sess.onMarketData(packer.finalize(), 1u);
    sess.stopCold();

    CHECK_EQ(sess.books().symbols(), 1u);
    CHECK(sess.books().book(99) == nullptr);
    CHECK_EQ(sess.book(0).bestBid(), 100);
    CHECK_EQ(sess.book(0).bestAsk(), 102);
    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.foreignMessages(), 0u);
    const dut::BookTable& cb = sess.cold()->books();
    CHECK_EQ(cb.symbols(), 2u);
    CHECK(cb.book(13) == nullptr);
    CHECK_EQ(cb.book(99)->bestBid(), 300);
    CHECK_EQ(cb.book(98)->bestAsk(), 400);
    CHECK_EQ(cb.liveOrders(), 2u);
    CHECK_EQ(sess.cold()->applied(), 6u);
    CHECK_EQ(sess.cold()->packets(), 1u);
    CHECK_EQ(sess.cold()->dropped(), 0u);
}

void test_transport_login_roundtrip() {
    dut::DutSession<dut::IoMode::Transport, JoinBid, MockIo> sess(baseCfg(), JoinBid{10u});
    MockIo                                                   io;
    net::Endpoints                                           oeEp{};
    oeEp.srcPort = 41001;
    oeEp.dstPort = 40001;
    CHECK(sess.prepareTransport(io, oeEp));
    CHECK(!sess.sessionEstablished());

    sess.sendLogin("SIM0000001", "DUT001");
    CHECK_EQ(io.frames.size(), 1u);
    const std::span<const std::byte> fr{reinterpret_cast<const std::byte*>(io.frames[0].data()),
                                        io.frames[0].size()};
    soup::Packet                     sp{};
    CHECK(soup::parse(fr.subspan(net::kL2L3L4Overhead), sp) != 0);
    CHECK(sp.type == soup::Type::LoginRequest);
    soup::LoginRequest lr{};
    std::memcpy(&lr, sp.payload.data(), sizeof lr);
    CHECK(lr.username.view() == std::string_view{"DUT001"});
    CHECK(lr.requestedSession.view() == std::string_view{"SIM0000001"});

    std::array<std::byte, 64> soupBuf{};
    const auto                ack = soup::packLoginAccepted(soupBuf.data(), "SIM0000001", 1);
    std::vector<std::uint8_t> frame(net::kL2L3L4Overhead + ack.size(), 0);
    frame[36] = static_cast<std::uint8_t>(41001u >> 8);
    frame[37] = static_cast<std::uint8_t>(41001u & 0xff);
    std::memcpy(frame.data() + net::kL2L3L4Overhead, ack.data(), ack.size());
    io.inbound.push_back(frame);
    sess.poll();
    CHECK(sess.sessionEstablished());
    CHECK_EQ(sess.packetsReceived(), 0u);
}

int main() {
    test_locate_filter_session_gating_and_reset();
    test_cold_shard_routes_unquoted_symbols();
    test_transport_login_roundtrip();
    test_quote_lifecycle_through_session();
    test_take_and_t2t();
    test_sequence_gap_and_stale();
    test_gap_pulls_live_quote();
    test_two_hot_symbols_resolved_from_directory();
    return abt::test::summary("dutsession");
}
