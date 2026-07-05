//
// Unit test for the DUT tick-to-trade session (abt::dut::DutSession) in Loopback mode:
// feed a real MoldUDP64 packet, confirm the book rebuilds and the strategy's order is emitted.
//

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
#include "abt/dut/TakeStrategy.hpp"
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

// One-shot test strategy: the first packet that establishes a bid triggers a single join-the-bid
// buy; deterministic, so the emitted-order count and fields are exactly assertable.
struct JoinBidOnce {
    Quantity qty;
    bool     armed = true;

    dut::OrderIntent onBook(const dut::BookBuilder& book) noexcept {
        dut::OrderIntent intent{};
        if (armed && book.bestBid() != kNoPrice) {
            intent = dut::OrderIntent{true, Side::Buy, book.bestBid(), qty};
            armed = false;
        }
        return intent;
    }
};

// Replays a queue of asynchronous TX-completion timestamps, mimicking the ef_vi eventq / DPDK
// timesync path the real transport will drive. status == 0 ends the drain.
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

std::span<const std::byte> packFeed(mold::Packer& packer, std::span<std::byte> buf) {
    packer.reset(buf.data(), buf.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    return packer.finalize();
}

void test_session() {
    mold::Packer packer("SESSION01", 1);
    std::array<std::byte, 2048> feedBuf{};
    const auto pkt = packFeed(packer, feedBuf);

    dut::DutConfig cfg{};
    cfg.minPrice = 1;
    cfg.maxPrice = 1000;
    cfg.tickWire = 1;
    cfg.symbol = "ABT";
    cfg.firstUserRef = 7;

    JoinBidOnce strat{10u};
    dut::DutSession<dut::IoMode::Loopback, JoinBidOnce> sess(cfg, strat);

    sess.onMarketData(pkt, 111u);

    CHECK_EQ(sess.book().bestBid(), 100);
    CHECK_EQ(sess.book().bestAsk(), 102);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 100), 500u);
    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.capturedOrders().size(), 1u);

    const auto& raw = sess.capturedOrders()[0];
    CHECK_EQ(raw.size(), sizeof(ouch::EnterOrder));
    ouch::EnterOrder o{};
    std::memcpy(&o, raw.data(), sizeof o);
    CHECK_EQ(o.type, static_cast<char>(ouch::InType::EnterOrder));
    CHECK_EQ(o.side, static_cast<char>(ouch::Side::Buy));
    CHECK_EQ(o.userRefNum.value(), 7u);
    CHECK_EQ(o.quantity.value(), 10u);
    CHECK_EQ(o.price.value(), 100u);
    CHECK(o.symbol.view() == std::string_view{"ABT"});

    // A second packet that does not improve the bid must not emit another order (one-shot armed).
    std::array<std::byte, 2048> feedBuf2{};
    packer.reset(feedBuf2.data(), feedBuf2.size());
    (void)packer.append(bytesOf(mkAdd(3u, 'B', 200u, 99)));
    sess.onMarketData(packer.finalize(), 222u);

    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.book().sizeAt(Side::Buy, 99), 200u);
}

void test_take_and_t2t() {
    static_assert(dut::TxStampSource<FakeStampSource>);

    dut::DutConfig cfg{};
    cfg.minPrice = 1;
    cfg.maxPrice = 1000;
    cfg.tickWire = 1;
    cfg.symbol = "ABT";
    cfg.firstUserRef = 1;

    // Cross the spread once when the offer reaches 101.
    dut::TakeStrategy strat(101, 5u);
    dut::DutSession<dut::IoMode::Loopback, dut::TakeStrategy> sess(cfg, strat);

    // Packet 1: offer sits at 102, above the trigger -> no order. RX stamp 500ns.
    mold::Packer packer("SESSION01", 1);
    std::array<std::byte, 2048> buf1{};
    packer.reset(buf1.data(), buf1.size());
    (void)packer.append(bytesOf(mkAdd(1u, 'B', 500u, 100)));
    (void)packer.append(bytesOf(mkAdd(2u, 'S', 300u, 102)));
    sess.onMarketData(packer.finalize(), 500u);
    CHECK_EQ(sess.ordersSent(), 0u);

    // Packet 2: a new offer at 101 trips the signal -> one marketable buy. RX stamp 1000ns.
    std::array<std::byte, 2048> buf2{};
    packer.reset(buf2.data(), buf2.size());
    (void)packer.append(bytesOf(mkAdd(3u, 'S', 200u, 101)));
    sess.onMarketData(packer.finalize(), 1000u);
    CHECK_EQ(sess.ordersSent(), 1u);
    CHECK_EQ(sess.book().bestAsk(), 101);

    const auto& raw = sess.capturedOrders()[0];
    ouch::EnterOrder o{};
    std::memcpy(&o, raw.data(), sizeof o);
    CHECK_EQ(o.side, static_cast<char>(ouch::Side::Buy));
    CHECK_EQ(o.price.value(), 101u);
    CHECK_EQ(o.quantity.value(), 5u);

    // No completion yet -> no sample.
    CHECK_EQ(sess.t2t().count(), 0u);

    // The NIC reports the order (userRef 1) left at 1000 + 850 ns -> t2t = 850 ns.
    FakeStampSource src{};
    src.queue.push_back(dut::TxCompletion{1u, 0u, 1850u, 1u});
    sess.pollTxCompletions(src);
    CHECK_EQ(sess.t2t().count(), 1u);
    CHECK_EQ(sess.t2t().min(), 850u);
    CHECK_EQ(sess.t2t().percentile(0.50), 850u);

    // A stale completion for an unknown order must not add a sample.
    FakeStampSource stale{};
    stale.queue.push_back(dut::TxCompletion{999u, 0u, 5000u, 1u});
    sess.pollTxCompletions(stale);
    CHECK_EQ(sess.t2t().count(), 1u);
}

}

int main() {
    test_session();
    test_take_and_t2t();
    return abt::test::summary("dutsession");
}
