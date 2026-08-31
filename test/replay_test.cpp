#include "TestHarness.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "abt/protocol/Itch50.hpp"
#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/replay/ItchFile.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "abt/sim/MarketReplay.hpp"
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
};

template <class T>
std::vector<std::byte> bytesOf(const T& m) {
    const auto* p = reinterpret_cast<const std::byte*>(&m);
    return {p, p + sizeof m};
}

std::vector<std::byte> systemEvent(char code, std::uint64_t ts) {
    itch::SystemEvent s{};
    s.messageType = 'S';
    s.stockLocate = 0;
    s.timestamp   = ts;
    s.eventCode   = code;
    return bytesOf(s);
}

constexpr std::uint64_t kOpen = (9ull * 3600 + 30 * 60) * 1'000'000'000ull;

std::vector<std::vector<std::byte>> makeDay(std::uint64_t spanNs, int steps) {
    std::vector<std::vector<std::byte>> out;
    std::uint64_t                       ts = kOpen;
    out.push_back(systemEvent('O', ts));
    out.push_back(systemEvent('Q', ts));
    RecSink        aapl;
    RecSink        other;
    Venue<RecSink> va(aapl, "AAPL", 13, 1, 100000, 100);
    Venue<RecSink> vo(other, "MSFT", 7, 1, 100000, 100);
    std::uint64_t  seed = 99;
    auto           rnd  = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    std::vector<OrderId> live;
    const std::uint64_t  step = spanNs / static_cast<std::uint64_t>(steps);
    for (int i = 0; i < steps; ++i) {
        ts += step;
        const std::size_t   before = aapl.md.size();
        const std::uint64_t roll   = rnd() % 100;
        if (roll < 30 && !live.empty()) {
            const std::size_t k = rnd() % live.size();
            va.cancelSynthetic(live[k], ts);
            live[k] = live.back();
            live.pop_back();
        } else {
            const bool  buy  = (rnd() & 1u) != 0;
            const Price tick = buy ? 31999 - static_cast<Price>(rnd() % 10)
                                   : 32001 + static_cast<Price>(rnd() % 10);
            live.push_back(va.injectSynthetic(buy ? Side::Buy : Side::Sell, tick, 100, ts));
        }
        for (std::size_t k = before; k < aapl.md.size(); ++k) {
            out.push_back(aapl.md[k]);
        }
        if (i % 5 == 0) {
            const std::size_t b = other.md.size();
            vo.injectSynthetic(Side::Sell, 3000, 10, ts);
            for (std::size_t k = b; k < other.md.size(); ++k) {
                out.push_back(other.md[k]);
            }
        }
    }
    out.push_back(systemEvent('M', ts + 1));
    out.push_back(systemEvent('C', ts + 2));
    return out;
}

std::string writeDay(const std::vector<std::vector<std::byte>>& msgs, const char* name) {
    std::string            path = name;
    replay::ItchFileWriter w(path);
    CHECK(w.ok());
    for (const auto& m : msgs) {
        w.write(m);
    }
    return path;
}

std::vector<std::vector<std::byte>> unpack(const std::vector<std::vector<std::byte>>& pkts) {
    std::vector<std::vector<std::byte>> out;
    for (const auto& p : pkts) {
        mold::forEachMessage({p.data(), p.size()}, [&](std::uint64_t, std::span<const std::byte> m) {
            out.emplace_back(m.begin(), m.end());
        });
    }
    return out;
}

ExchangeConfig venueCfg() {
    ExchangeConfig c;
    c.symbol        = "AAPL";
    c.stockLocate   = 13;
    c.firstOrderRef = 1ull << 62;
    c.mdMaxPayload  = 400;
    return c;
}

void test_afap_forwards_verbatim_and_loops() {
    const auto        day  = makeDay(60'000'000'000ull, 3000);
    const std::string path = writeDay(day, "replay_afap.itch");

    ExchangeSession<IoMode::Loopback> ex{venueCfg()};
    ReplayConfig                      rc;
    rc.file  = path;
    rc.speed = 0.0;
    rc.loops = 2;
    MarketReplay<ExchangeSession<IoMode::Loopback>> rp(ex, rc);
    CHECK(rp.open());
    CHECK(rp.progress().preloaded);
    CHECK_EQ(rp.fileMessages(), day.size());

    std::size_t pumps = 0;
    while (rp.pump(monotonicNs())) {
        ++pumps;
    }
    CHECK(rp.progress().finished);
    CHECK_EQ(rp.progress().loop, 2u);
    CHECK_EQ(rp.progress().sent, 2 * day.size());
    CHECK_EQ(ex.stats().forwarded, 2 * day.size());
    CHECK(ex.stats().mirrored > 0);
    CHECK_EQ(ex.mirrorStats().unknownRef, 0u);
    CHECK_EQ(ex.mirrorStats().overReduce, 0u);
    CHECK_EQ(ex.liveOrders(), 0u);

    const auto got = unpack(ex.capturedMarketData());
    CHECK_EQ(got.size(), 2 * day.size());
    bool same = got.size() == 2 * day.size();
    for (std::size_t i = 0; same && i < got.size(); ++i) {
        same = got[i] == day[i % day.size()];
    }
    CHECK(same);
    CHECK(ex.capturedMarketData().size() < got.size() / 4);
    CHECK(pumps >= got.size() / rc.maxBatch);
}

void test_streamed_when_over_preload_limit() {
    const auto                        day  = makeDay(1'000'000'000ull, 500);
    const std::string                 path = writeDay(day, "replay_stream.itch");
    ExchangeSession<IoMode::Loopback> ex{venueCfg()};
    ReplayConfig                      rc;
    rc.file         = path;
    rc.speed        = 0.0;
    rc.loops        = 3;
    rc.preloadMaxMb = 0;
    MarketReplay<ExchangeSession<IoMode::Loopback>> rp(ex, rc);
    CHECK(rp.open());
    CHECK(!rp.progress().preloaded);
    while (rp.pump(monotonicNs())) {
    }
    CHECK_EQ(rp.progress().sent, 3 * day.size());
    CHECK_EQ(unpack(ex.capturedMarketData()).size(), 3 * day.size());
}

void test_paced_replay_takes_virtual_time() {
    const auto                        day  = makeDay(2'000'000'000ull, 200);
    const std::string                 path = writeDay(day, "replay_paced.itch");
    ExchangeSession<IoMode::Loopback> ex{venueCfg()};
    ReplayConfig                      rc;
    rc.file  = path;
    rc.speed = 50.0;
    rc.loops = 1;
    MarketReplay<ExchangeSession<IoMode::Loopback>> rp(ex, rc);
    CHECK(rp.open());
    const auto t0 = std::chrono::steady_clock::now();
    while (rp.pump(monotonicNs())) {
    }
    const auto el =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(el >= 38);
    CHECK(el < 400);
    CHECK_EQ(rp.progress().sent, day.size());
    const auto pkts = ex.capturedMarketData().size();
    CHECK(pkts > day.size() / 2);
    CHECK(rp.progress().maxLateNs < 5'000'000ull);
}

void test_skip_to_and_stop_at() {
    const auto                        day  = makeDay(4'000'000'000ull, 400);
    const std::string                 path = writeDay(day, "replay_window.itch");
    ExchangeSession<IoMode::Loopback> ex{venueCfg()};
    ReplayConfig                      rc;
    rc.file     = path;
    rc.speed    = 100.0;
    rc.loops    = 1;
    rc.skipToNs = kOpen + 3'000'000'000ull;
    rc.stopAtNs = kOpen + 3'500'000'000ull;
    MarketReplay<ExchangeSession<IoMode::Loopback>> rp(ex, rc);
    CHECK(rp.open());
    const auto t0 = std::chrono::steady_clock::now();
    while (rp.pump(monotonicNs())) {
    }
    const auto el =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(el >= 4);
    CHECK(el < 200);
    CHECK(rp.progress().sent < day.size());
    CHECK(rp.progress().sent > day.size() * 3 / 4);
    CHECK(rp.progress().virtualTs <= rc.stopAtNs);
    CHECK(rp.progress().virtualTs > rc.skipToNs);
}

void test_client_order_survives_replay_and_reset_cancels_it() {
    const auto                        day  = makeDay(1'000'000'000ull, 300);
    const std::string                 path = writeDay(day, "replay_client.itch");
    ExchangeSession<IoMode::Loopback> ex{venueCfg()};
    ReplayConfig                      rc;
    rc.file  = path;
    rc.speed = 0.0;
    rc.loops = 1;
    MarketReplay<ExchangeSession<IoMode::Loopback>> rp(ex, rc);
    CHECK(rp.open());
    CHECK(rp.pump(monotonicNs()));

    ouch::EnterOrder o{};
    o.type               = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum         = 1;
    o.side               = 'B';
    o.quantity           = 100;
    o.symbol             = std::string_view{"AAPL"};
    o.price              = 3'100'000;
    o.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    o.display            = static_cast<char>(ouch::Display::Visible);
    o.capacity           = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType          = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId            = std::string_view{"CID"};
    o.appendageLength    = 0;
    std::array<std::byte, 256> buf{};
    const auto                 pkt = soup::packUnsequencedData(buf.data(), bytesOf(o));
    ex.onOrderEntryBytes(pkt, kOpen);
    CHECK_EQ(ex.clientOrders(), 1u);

    while (rp.pump(monotonicNs())) {
    }
    CHECK_EQ(ex.clientOrders(), 0u);
    std::size_t canceled = 0;
    for (const auto& p : ex.capturedOrderEntry()) {
        soup::Packet sp{};
        (void)soup::parse({p.data(), p.size()}, sp);
        if (!sp.payload.empty() && static_cast<char>(sp.payload[0]) == 'C') {
            ++canceled;
        }
    }
    CHECK_EQ(canceled, 1u);
    const auto got = unpack(ex.capturedMarketData());
    CHECK(got.size() == day.size() + 2);
    std::size_t clientAdds = 0;
    for (const auto& m : got) {
        if (static_cast<char>(m[0]) == 'A') {
            itch::AddOrder a{};
            std::memcpy(&a, m.data(), sizeof a);
            if (a.orderRef.value() >= (1ull << 62)) {
                ++clientAdds;
            }
        }
    }
    CHECK_EQ(clientAdds, 1u);
}

}   // namespace

int main() {
    test_afap_forwards_verbatim_and_loops();
    test_streamed_when_over_preload_limit();
    test_paced_replay_takes_virtual_time();
    test_skip_to_and_stop_at();
    test_client_order_survives_replay_and_reset_cancels_it();
    return abt::test::summary("replay_test");
}
