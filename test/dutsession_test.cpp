#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "TestHarness.hpp"

#include "abt/dut/DutSession.hpp"
#include "abt/dut/Strategy.hpp"
#include "abt/dut/TxStamp.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"

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

struct JoinBid {
    Quantity qty;

    dut::QuoteTargets onBook(const dut::BookBuilder& book, const dut::Account&) noexcept {
        dut::QuoteTargets t{};
        if (book.bestBid() != kNoPrice) {
            t.quoteBid = true;
            t.bidPrice = book.bestBid();
            t.bidQty = qty;
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
        const Price ask = book.bestAsk();
        if (ask == kNoPrice || ask > trigger || !armed) {
            return t;
        }
        armed = false;
        t.quoteBid = true;
        t.bidPrice = ask;
        t.bidQty = qty;
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
        return dut::TxCompletion{0, 0, 0, 0};
    }
};

static_assert(dut::Strategy<JoinBid>);
static_assert(dut::Strategy<TakeOnce>);
static_assert(dut::TxStampSource<FakeStampSource>);

dut::DutConfig baseCfg() {
    dut::DutConfig cfg{};
    cfg.minPrice = 1;
    cfg.maxPrice = 1000;
    cfg.tickWire = 1;
    cfg.symbol = "ABT";
    cfg.firstUserRef = 7;
    return cfg;
}

ouch::Accepted accepted(std::uint32_t ref, Quantity qty) {
    ouch::Accepted a{};
    a.type = 'A';
    a.userRefNum = ref;
    a.quantity = qty;
    a.orderState = 'L';
    return a;
}

void test_quote_lifecycle_through_session() {
    mold::Packer packer("SESSION01", 1);
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
    CHECK_EQ(o.type, static_cast<char>(ouch::InType::EnterOrder));
    CHECK_EQ(o.side, static_cast<char>(ouch::Side::Buy));
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
    CHECK_EQ(u.type, static_cast<char>(ouch::InType::ReplaceOrder));
    CHECK_EQ(u.origUserRefNum.value(), 7u);
    CHECK_EQ(u.userRefNum.value(), 8u);
    CHECK_EQ(u.price.value(), 101u);
    CHECK(sess.oms().slot(Side::Buy).state == dut::QuoteState::PendingReplace);

    (void)sess.proc().drain();
    CHECK_EQ(sess.proc().count(), 3);
    CHECK_EQ(sess.feed().gaps(), 0u);
}

void test_take_and_t2t() {
    dut::DutConfig cfg = baseCfg();
    cfg.firstUserRef = 1;
    dut::DutSession<dut::IoMode::Loopback, TakeOnce> sess(cfg, TakeOnce{101, 5u});

    mold::Packer packer("SESSION01", 1);
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
    CHECK_EQ(o.side, static_cast<char>(ouch::Side::Buy));
    CHECK_EQ(o.price.value(), 101u);
    CHECK_EQ(o.quantity.value(), 5u);

    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 0);

    FakeStampSource src{};
    src.queue.push_back(dut::TxCompletion{1u, 0u, 1850u, 1u});
    sess.pollTxCompletions(src);
    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 1);
    CHECK_EQ(sess.t2t().min(), 850);
    CHECK_EQ(sess.t2t().percentile(50.0), 850);

    FakeStampSource stale{};
    stale.queue.push_back(dut::TxCompletion{999u, 0u, 5000u, 1u});
    sess.pollTxCompletions(stale);
    (void)sess.t2t().drain();
    CHECK_EQ(sess.t2t().count(), 1);
}

void test_sequence_gap_and_stale() {
    dut::DutSession<dut::IoMode::Loopback, JoinBid> sess(baseCfg(), JoinBid{10u});
    std::array<std::byte, 2048> buf{};

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
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 99), 100u);

    mold::Packer p3("SESSION01", 3);
    p3.reset(buf.data(), buf.size());
    (void)p3.append(bytesOf(mkAdd(3u, 'B', 777u, 98)));
    sess.onMarketData(p3.finalize(), 3u);
    CHECK_EQ(sess.feed().stale(), 1u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 98), 0u);
    CHECK_EQ(sess.feed().expected(), 6u);
}

}

int main() {
    test_quote_lifecycle_through_session();
    test_take_and_t2t();
    test_sequence_gap_and_stale();
    return abt::test::summary("dutsession");
}
