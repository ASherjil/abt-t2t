#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/Ouch50.hpp"
#include "t2t/replay/BookReplay.hpp"
#include "t2t/replay/ItchFile.hpp"
#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/sim/Venue.hpp"

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

std::vector<std::byte> stockDirectory(std::uint16_t locate, std::string_view sym, std::uint64_t ts) {
    itch::StockDirectory r{};
    r.messageType    = 'R';
    r.stockLocate    = locate;
    r.timestamp      = ts;
    r.stock          = sym;
    r.marketCategory = 'Q';
    r.roundLotSize   = 100;
    return bytesOf(r);
}

std::vector<std::byte> systemEvent(char code, std::uint64_t ts) {
    itch::SystemEvent s{};
    s.messageType = 'S';
    s.stockLocate = 0;
    s.timestamp   = ts;
    s.eventCode   = code;
    return bytesOf(s);
}

ouch::EnterOrder aggressor(std::uint32_t user, char side, std::uint32_t qty, std::uint64_t px) {
    ouch::EnterOrder o{};
    o.type               = ouch::InType::EnterOrder;
    o.userRefNum         = user;
    o.side               = static_cast<ouch::Side>(side);
    o.quantity           = qty;
    o.symbol             = std::string_view{"AAPL"};
    o.price              = px;
    o.timeInForce        = ouch::TimeInForce::Day;
    o.display            = ouch::Display::Visible;
    o.capacity           = ouch::Capacity::Agency;
    o.imSweepEligibility = ouch::ImSweep::NotEligible;
    o.crossType          = ouch::CrossType::Continuous;
    o.appendageLength    = 0;
    return o;
}

struct Fixture {
    std::vector<std::vector<std::byte>> messages;
    std::size_t                         aaplMessages = 0;
    std::size_t                         msftMessages = 0;
};

Fixture makeDay() {
    Fixture             f;
    const std::uint64_t open = 9ull * 3600 + static_cast<unsigned long long>(30 * 60);
    std::uint64_t       ts   = open * 1'000'000'000ull;
    f.messages.push_back(systemEvent('O', ts));
    f.messages.push_back(stockDirectory(1, "AAPL", ts));
    f.messages.push_back(stockDirectory(2, "MSFT", ts));
    f.messages.push_back(systemEvent('Q', ts));

    RecSink        aapl;
    RecSink        msft;
    Venue<RecSink> va(aapl, "AAPL", 1, 1, 100000, 100);
    Venue<RecSink> vm(msft, "MSFT", 2, 1, 100000, 100);
    std::uint64_t  seed = 42;
    auto           rnd  = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    std::vector<OrderId> live;
    std::uint32_t        user = 1;
    for (int i = 0; i < 4000; ++i) {
        ts += 1'000 + rnd() % 200'000;
        const std::size_t before = aapl.md.size();
        const int         roll   = static_cast<int>(rnd() % 100);
        if (roll < 25 && !live.empty()) {
            const std::size_t k = rnd() % live.size();
            va.cancelSynthetic(live[k], ts);
            live[k] = live.back();
            live.pop_back();
        } else if (roll < 40) {
            const bool  buy  = (rnd() & 1u) != 0;
            const Price tick = buy ? (va.bestAsk() != kNoPrice ? va.bestAsk() : 5200)
                                   : (va.bestBid() != kNoPrice ? va.bestBid() : 5200);
            va.onEnterOrder(aggressor(user++, buy ? 'B' : 'S', static_cast<std::uint32_t>(50 + rnd() % 400),
                                      static_cast<std::uint64_t>(tick) * 100),
                            ts);
        } else {
            const bool  buy  = (rnd() & 1u) != 0;
            const Price off  = static_cast<Price>(rnd() % 10);
            const Price tick = buy ? 5199 - off : 5201 + off;
            live.push_back(va.injectSynthetic(buy ? Side::Buy : Side::Sell, tick,
                                              static_cast<Quantity>(100 + rnd() % 300), ts));
        }
        for (std::size_t k = before; k < aapl.md.size(); ++k) {
            f.messages.push_back(aapl.md[k]);
            ++f.aaplMessages;
        }
        if ((i % 7) == 0) {
            const std::size_t mb = msft.md.size();
            vm.injectSynthetic(Side::Buy, 3000 - static_cast<Price>(rnd() % 5), 100, ts);
            for (std::size_t k = mb; k < msft.md.size(); ++k) {
                f.messages.push_back(msft.md[k]);
                ++f.msftMessages;
            }
        }
    }
    ts += 1'000'000;
    f.messages.push_back(systemEvent('M', ts));
    f.messages.push_back(systemEvent('C', ts));
    return f;
}

void writePlain(const std::string& path, const Fixture& f) {
    replay::ItchFileWriter w(path);
    CHECK(w.ok());
    for (const auto& m : f.messages) {
        w.write(m);
    }
    CHECK_EQ(w.messages(), f.messages.size());
}

void writeGz(const std::string& path, const Fixture& f) {
    gzFile g = gzopen(path.c_str(), "wb");
    CHECK(g != nullptr);
    for (const auto& m : f.messages) {
        const unsigned char len[2] = {static_cast<unsigned char>(m.size() >> 8),
                                      static_cast<unsigned char>(m.size() & 0xFFu)};
        gzwrite(g, len, 2);
        gzwrite(g, m.data(), static_cast<unsigned>(m.size()));
    }
    gzclose(g);
}

void test_roundtrip_and_replay(const std::string& path, const Fixture& f) {
    replay::ItchFileReader r(path);
    CHECK(r.ok());
    replay::SymbolFilter       filter("AAPL");
    replay::BookReplay         book(1, 0, 20'000'000, 100);
    std::span<const std::byte> msg;
    std::size_t                kept = 0;
    std::size_t                idx  = 0;
    while (r.next(msg)) {
        CHECK_EQ(msg.size(), f.messages[idx].size());
        ++idx;
        if (filter.accept(msg)) {
            ++kept;
            book.onMessage(msg);
        }
    }
    book.finish();
    CHECK(!r.truncated());
    CHECK_EQ(r.messages(), f.messages.size());
    CHECK(filter.resolved());
    CHECK_EQ(filter.stockLocate(), 1u);
    CHECK_EQ(kept, f.aaplMessages + 5u);

    const replay::ReplayStats& s = book.stats();
    CHECK_EQ(s.messages, f.aaplMessages);
    CHECK(s.adds > 0 && s.executes > 0 && s.deletes > 0);
    CHECK_EQ(s.unknownRef, 0u);
    CHECK_EQ(s.overReduce, 0u);
    CHECK_EQ(s.crossed, 0u);
    CHECK_EQ(s.outOfBand, 0u);
    CHECK(s.maxLive > 0);
    CHECK(s.marketOpenTs > 0 && s.marketCloseTs > s.marketOpenTs);
    CHECK(s.peakPerMs >= 1);
    CHECK(book.interArrivalNs().count() > 0);
}

void test_extract_then_replay(const std::string& src) {
    const std::string out = "itchfile_test_aapl.itch";
    {
        replay::ItchFileReader     r(src);
        replay::ItchFileWriter     w(out);
        replay::SymbolFilter       filter("AAPL");
        std::span<const std::byte> msg;
        while (r.next(msg)) {
            if (filter.accept(msg)) {
                w.write(msg);
            }
        }
    }
    replay::ItchFileReader     r(out);
    replay::SymbolFilter       filter("AAPL");
    replay::BookReplay         book(1, 0, 20'000'000, 100);
    std::span<const std::byte> msg;
    std::size_t                msftSeen = 0;
    while (r.next(msg)) {
        if (static_cast<char>(msg[0]) != 'S' && replay::SymbolFilter::locateOf(msg) == 2) {
            ++msftSeen;
        }
        CHECK(filter.accept(msg));
        book.onMessage(msg);
    }
    CHECK_EQ(msftSeen, 0u);
    CHECK_EQ(book.stats().unknownRef, 0u);
    std::remove(out.c_str());
}

void test_truncated() {
    const std::string path = "itchfile_test_trunc.itch";
    {
        std::FILE*          fp      = std::fopen(path.c_str(), "wb");
        const unsigned char bytes[] = {0, 12, 'S', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Q', 0, 36, 'A', 0, 1};
        std::fwrite(bytes, 1, sizeof bytes, fp);
        std::fclose(fp);
    }
    replay::ItchFileReader     r(path);
    std::span<const std::byte> msg;
    CHECK(r.next(msg));
    CHECK_EQ(msg.size(), 12u);
    CHECK(!r.next(msg));
    CHECK(r.truncated());
    std::remove(path.c_str());
}

void test_time_format() {
    CHECK(replay::formatTimeOfDay(34'200'000'000'000ull) == "09:30:00.000");
    CHECK(replay::formatTimeOfDay(57'600'123'456'789ull) == "16:00:00.123");
}

}   // namespace

int main() {
    const Fixture     f     = makeDay();
    const std::string plain = "itchfile_test_day.itch";
    const std::string gz    = "itchfile_test_day.itch.gz";
    writePlain(plain, f);
    writeGz(gz, f);
    test_roundtrip_and_replay(plain, f);
    test_roundtrip_and_replay(gz, f);
    test_extract_then_replay(gz);
    test_truncated();
    test_time_format();
    std::remove(plain.c_str());
    std::remove(gz.c_str());
    return abt::test::summary("itchfile");
}
