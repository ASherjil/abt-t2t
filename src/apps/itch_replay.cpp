#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sys/resource.h>

#include <fmt/core.h>

#include "t2t/replay/BookReplay.hpp"
#include "t2t/replay/FeedValidator.hpp"
#include "t2t/replay/ItchFile.hpp"
#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/util/Clock.hpp"
#include "t2t/util/HugePageArena.hpp"

using namespace abt;

namespace {

struct Args {
    std::string                file;
    std::vector<std::string>   symbols;
    bool                       all = false;
    std::optional<std::string> extractTo;
    Price                      minPrice      = 0;
    Price                      maxPrice      = 20'000'000;
    Price                      tickWire      = 100;
    std::size_t                bandTicks     = 2048;
    std::size_t                arenaMb       = 0;
    std::uint64_t              progressEvery = 50'000'000;
};

void usage() {
    fmt::print(stderr, "usage: itch_replay <file.itch|.gz> <SYMBOL[,SYMBOL...]|--all> [--extract <out.itch>] "
                       "[--max-price <wire>] [--tick <wire>] [--band <ticks>] [--arena-mb <MB>]\n");
}

std::vector<std::string> splitSymbols(std::string_view list) {
    std::vector<std::string> out;
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const auto        part  = list.substr(0, comma);
        if (!part.empty()) {
            out.emplace_back(part);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return out;
}

bool parseArgs(int argc, char** argv, Args& a) {
    if (argc < 3) {
        return false;
    }
    a.file = argv[1];
    if (std::string_view{argv[2]} == "--all") {
        a.all = true;
    } else {
        a.symbols = splitSymbols(argv[2]);
        if (a.symbols.empty()) {
            return false;
        }
    }
    for (int i = 3; i < argc; ++i) {
        const std::string_view opt      = argv[i];
        const bool             hasValue = i + 1 < argc;
        if (opt == "--extract" && hasValue) {
            a.extractTo = argv[++i];
        } else if (opt == "--max-price" && hasValue) {
            a.maxPrice = static_cast<Price>(std::atol(argv[++i]));
        } else if (opt == "--tick" && hasValue) {
            a.tickWire = static_cast<Price>(std::atol(argv[++i]));
        } else if (opt == "--band" && hasValue) {
            a.bandTicks = static_cast<std::size_t>(std::atol(argv[++i]));
        } else if (opt == "--arena-mb" && hasValue) {
            a.arenaMb = static_cast<std::size_t>(std::atol(argv[++i]));
        } else {
            return false;
        }
    }
    return true;
}

int runValidator(const Args& a, replay::ItchFileReader& reader,
                 std::optional<replay::ItchFileWriter>& writer) {
    const std::uint64_t  startNs = monotonicNs();
    util::HugePageArena  arena(a.arenaMb << 20);
    dut::BookTableConfig cfg{};
    cfg.tickWire      = a.tickWire;
    cfg.coldBandTicks = a.bandTicks;
    cfg.hotBandTicks  = a.bandTicks;
    cfg.bandFraction  = 0.10;
    cfg.coldMapSlots  = 1024;
    cfg.subDollarTick = 1;
    cfg.memory        = a.arenaMb != 0 ? arena.resource() : nullptr;
    replay::FeedValidator               validator(cfg);
    std::optional<replay::SymbolFilter> filter;
    if (!a.all) {
        filter.emplace(a.symbols);
    }
    std::span<const std::byte> msg;
    std::uint64_t              kept = 0;
    while (reader.next(msg)) {
        if (reader.messages() % a.progressEvery == 0) {
            fmt::print(stderr, "  ... {} messages read, {} kept, {} books\n", reader.messages(), kept,
                       validator.books().symbols());
        }
        if (filter && !filter->accept(msg)) {
            continue;
        }
        ++kept;
        if (writer) {
            writer->write(msg);
        }
        validator.onMessage(msg);
    }
    if (reader.truncated()) {
        fmt::print(stderr, "itch_replay: warning: input ended mid-message (truncated file)\n");
    }
    if (filter && !filter->resolved()) {
        fmt::print(stderr, "itch_replay: only {} of {} symbols found in the stock directory\n",
                   filter->resolvedCount(), a.symbols.size());
    }

    const replay::FeedTotals t = validator.totals();
    fmt::print("file           {}\n", a.file);
    fmt::print("scope          {}{}\n", a.all ? "all symbols" : fmt::format("{} symbols", a.symbols.size()),
               writer ? fmt::format(", extracted {} msgs to {}", writer->messages(), *a.extractTo) : "");
    fmt::print("read           {} messages, {} bytes; validated {} across {} books ({} arena)\n",
               reader.messages(), reader.bytes(), t.messages, t.symbols,
               a.arenaMb != 0 ? (arena.huge() ? "hugetlb" : "4K-page fallback") : "heap");
    fmt::print("invariants     unknown ref {}  over-reduce {}  crossed {}  locked {}  out-of-band adds {}\n",
               t.unknownRef, t.overReduce, t.crossed, t.locked, t.outOfBand);
    fmt::print("anchoring      re-anchors on out-of-band trades {}  sub-dollar tick books {}\n", t.reanchors,
               t.subDollar);
    fmt::print("book           band {} ticks/side (min), max live orders {} (sampled every 64k msgs)\n",
               a.bandTicks, t.maxLive);
    rusage ru{};
    (void)getrusage(RUSAGE_SELF, &ru);
    const double elapsed = static_cast<double>(monotonicNs() - startNs) * 1e-9;
    fmt::print("resources      {:.1f} s wall, {:.1f} M msg/s, peak RSS {} MB, live book footprint {} MB\n",
               elapsed, static_cast<double>(reader.messages()) / elapsed / 1e6, ru.ru_maxrss / 1024,
               validator.books().footprintBytes() >> 20);

    std::vector<std::size_t> order;
    const auto&              per = validator.perSymbol();
    for (std::size_t l = 0; l < per.size(); ++l) {
        if (per[l].messages > 0) {
            order.push_back(l);
        }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
        return per[x].messages > per[y].messages;
    });
    fmt::print("{:<10}{:>7}{:>12}{:>9}{:>9}{:>9}{:>9}{:>9}\n", "symbol", "locate", "msgs", "unknown", "over",
               "crossed", "locked", "maxlive");
    std::size_t shown = 0;
    for (const std::size_t l : order) {
        const replay::SymbolStats& s     = per[l];
        const bool                 fault = s.unknownRef + s.overReduce + s.crossed > 0;
        if (shown < 20 || fault) {
            fmt::print("{:<10}{:>7}{:>12}{:>9}{:>9}{:>9}{:>9}{:>9}{}\n", s.name.empty() ? "?" : s.name, l,
                       s.messages, s.unknownRef, s.overReduce, s.crossed, s.locked, s.maxLive,
                       fault ? "  <-- FAULT" : "");
        }
        ++shown;
    }
    return (t.unknownRef + t.overReduce + t.crossed) == 0 ? 0 : 3;
}

}   // namespace

int main(int argc, char** argv) {
    Args a{};
    if (!parseArgs(argc, argv, a)) {
        usage();
        return 2;
    }

    replay::ItchFileReader reader(a.file);
    if (!reader.ok()) {
        fmt::print(stderr, "itch_replay: cannot open {}\n", a.file);
        return 1;
    }
    std::optional<replay::ItchFileWriter> writer;
    if (a.extractTo) {
        writer.emplace(*a.extractTo);
        if (!writer->ok()) {
            fmt::print(stderr, "itch_replay: cannot create {}\n", *a.extractTo);
            return 1;
        }
    }

    if (a.all || a.symbols.size() > 1) {
        return runValidator(a, reader, writer);
    }

    replay::SymbolFilter              filter(a.symbols[0]);
    std::optional<replay::BookReplay> book;
    std::span<const std::byte>        msg;
    std::uint64_t                     kept = 0;
    while (reader.next(msg)) {
        if (reader.messages() % a.progressEvery == 0) {
            fmt::print(stderr, "  ... {} messages read, {} kept\n", reader.messages(), kept);
        }
        if (!filter.accept(msg)) {
            continue;
        }
        ++kept;
        if (writer) {
            writer->write(msg);
        }
        if (!book && filter.resolved()) {
            book.emplace(filter.stockLocate(), a.minPrice, a.maxPrice, a.tickWire);
        }
        if (book) {
            book->onMessage(msg);
        }
    }
    if (reader.truncated()) {
        fmt::print(stderr, "itch_replay: warning: input ended mid-message (truncated file)\n");
    }
    if (!filter.resolved()) {
        fmt::print(stderr, "itch_replay: symbol {} not found in stock directory\n", a.symbols[0]);
        return 1;
    }
    book->finish();

    const replay::ReplayStats& s   = book->stats();
    const util::Histogram&     gap = book->interArrivalNs();
    fmt::print("file           {}\n", a.file);
    fmt::print("symbol         {} (locate {})\n", a.symbols[0], filter.stockLocate());
    fmt::print("read           {} messages, {} bytes{}\n", reader.messages(), reader.bytes(),
               writer ? fmt::format(", extracted {} to {}", writer->messages(), *a.extractTo) : "");
    fmt::print("symbol msgs    {}  (A/F {}  E/C {}  X {}  D {}  U {}  P/Q {})\n", s.messages, s.adds,
               s.executes, s.cancels, s.deletes, s.replaces, s.trades);
    fmt::print("time span      {} -> {}   market hours {} -> {}\n", replay::formatTimeOfDay(s.firstTs),
               replay::formatTimeOfDay(s.lastTs), replay::formatTimeOfDay(s.marketOpenTs),
               replay::formatTimeOfDay(s.marketCloseTs));
    fmt::print("invariants     unknown ref {}  over-reduce {}  crossed {}  out-of-band adds {}\n",
               s.unknownRef, s.overReduce, s.crossed, s.outOfBand);
    fmt::print("book           max live orders {}  final live {}  final bid {}  ask {}\n", s.maxLive,
               book->book().liveOrders(), book->book().bestBid(), book->book().bestAsk());
    fmt::print("feed rate      peak {} msg/ms at {}   peak {} msg/s at {}\n", s.peakPerMs,
               replay::formatTimeOfDay(s.peakMsBucket * 1'000'000ull), s.peakPerSec,
               replay::formatTimeOfDay(s.peakSecBucket * 1'000'000'000ull));
    fmt::print("feed gap       p50 {} ns  p90 {} ns  p99 {} ns  p99.9 {} ns  min {} ns\n",
               gap.percentile(50.0), gap.percentile(90.0), gap.percentile(99.0), gap.percentile(99.9),
               gap.min());
    return 0;
}
