#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include "abt/replay/BookReplay.hpp"
#include "abt/replay/ItchFile.hpp"
#include "abt/replay/SymbolFilter.hpp"

using namespace abt;

namespace {

struct Args {
    std::string file;
    std::string symbol;
    std::optional<std::string> extractTo;
    Price minPrice = 0;
    Price maxPrice = 20'000'000;
    Price tickWire = 100;
    std::uint64_t progressEvery = 50'000'000;
};

void usage() {
    fmt::print(stderr,
               "usage: itch_replay <file.itch|.gz> <SYMBOL> [--extract <out.itch>] "
               "[--max-price <wire>] [--tick <wire>]\n");
}

bool parseArgs(int argc, char** argv, Args& a) {
    if (argc < 3) {
        return false;
    }
    a.file = argv[1];
    a.symbol = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string_view opt = argv[i];
        const bool hasValue = i + 1 < argc;
        if (opt == "--extract" && hasValue) {
            a.extractTo = argv[++i];
        } else if (opt == "--max-price" && hasValue) {
            a.maxPrice = static_cast<Price>(std::atol(argv[++i]));
        } else if (opt == "--tick" && hasValue) {
            a.tickWire = static_cast<Price>(std::atol(argv[++i]));
        } else {
            return false;
        }
    }
    return true;
}

}

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

    replay::SymbolFilter filter(a.symbol);
    std::optional<replay::BookReplay> book;
    std::span<const std::byte> msg;
    std::uint64_t kept = 0;
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
        fmt::print(stderr, "itch_replay: symbol {} not found in stock directory\n", a.symbol);
        return 1;
    }
    book->finish();

    const replay::ReplayStats& s = book->stats();
    const util::Histogram& gap = book->interArrivalNs();
    fmt::print("file           {}\n", a.file);
    fmt::print("symbol         {} (locate {})\n", a.symbol, filter.stockLocate());
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
