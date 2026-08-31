#include "abt/replay/BookReplay.hpp"

#include "abt/protocol/Itch50.hpp"

namespace abt::replay {

namespace {

template <class M>
[[nodiscard]] const M* as(std::span<const std::byte> msg) noexcept {
    if (msg.size() < sizeof(M)) {
        return nullptr;
    }
    return reinterpret_cast<const M*>(msg.data());
}

[[nodiscard]] std::uint16_t locateOf(std::span<const std::byte> msg) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                      std::to_integer<unsigned>(msg[2]));
}

[[nodiscard]] std::uint64_t timestampOf(std::span<const std::byte> msg) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 5; i < 11; ++i) {
        v = (v << 8) | std::to_integer<std::uint64_t>(msg[i]);
    }
    return v;
}

}

BookReplay::BookReplay(std::uint16_t stockLocate, Price minPrice, Price maxPrice, Price tickWire)
    : m_locate(stockLocate),
      m_minPrice(minPrice),
      m_maxPrice(maxPrice),
      m_book(minPrice, maxPrice, tickWire, 1u << 16),
      m_gap(1, 100'000'000'000ll, 3) {
}

void BookReplay::onMessage(std::span<const std::byte> msg) {
    if (msg.size() < 11) {
        return;
    }
    const char type = static_cast<char>(msg[0]);
    const std::uint16_t locate = locateOf(msg);
    const std::uint64_t ts = timestampOf(msg);

    if (type == 'S') {
        const auto* s = as<itch::SystemEvent>(msg);
        if (s == nullptr) {
            return;
        }
        switch (static_cast<itch::SystemEventCode>(s->eventCode)) {
            case itch::SystemEventCode::StartOfMarketHours:
                m_marketHours = true;
                m_stats.marketOpenTs = ts;
                break;
            case itch::SystemEventCode::EndOfMarketHours:
                m_marketHours = false;
                m_stats.marketCloseTs = ts;
                break;
            default:
                break;
        }
        return;
    }
    if (locate != m_locate) {
        return;
    }
    if (type == 'H') {
        const auto* h = as<itch::StockTradingAction>(msg);
        if (h != nullptr) {
            m_trading = h->tradingState == static_cast<char>(itch::TradingState::Trading);
        }
        return;
    }

    ++m_stats.messages;
    if (m_stats.messages == 1) {
        m_stats.firstTs = ts;
    }
    m_stats.lastTs = ts;
    rate(ts);

    switch (type) {
        case 'A':
        case 'F': {
            const auto* a = as<itch::AddOrder>(msg);
            if (a == nullptr) {
                return;
            }
            ++m_stats.adds;
            const auto price = static_cast<Price>(a->price.value());
            if (price < m_minPrice || price > m_maxPrice) {
                ++m_stats.outOfBand;
            }
            break;
        }
        case 'E': {
            const auto* e = as<itch::OrderExecuted>(msg);
            if (e == nullptr) {
                return;
            }
            ++m_stats.executes;
            checkReference(e->orderRef.value(), e->executedShares.value());
            break;
        }
        case 'C': {
            const auto* c = as<itch::OrderExecutedWithPrice>(msg);
            if (c == nullptr) {
                return;
            }
            ++m_stats.executes;
            checkReference(c->orderRef.value(), c->executedShares.value());
            break;
        }
        case 'X': {
            const auto* x = as<itch::OrderCancel>(msg);
            if (x == nullptr) {
                return;
            }
            ++m_stats.cancels;
            checkReference(x->orderRef.value(), x->cancelledShares.value());
            break;
        }
        case 'D': {
            const auto* d = as<itch::OrderDelete>(msg);
            if (d == nullptr) {
                return;
            }
            ++m_stats.deletes;
            checkReference(d->orderRef.value(), 0);
            break;
        }
        case 'U': {
            const auto* u = as<itch::OrderReplace>(msg);
            if (u == nullptr) {
                return;
            }
            ++m_stats.replaces;
            checkReference(u->origOrderRef.value(), 0);
            break;
        }
        case 'P':
        case 'Q': {
            ++m_stats.trades;
            return;
        }
        default: {
            return;
        }
    }

    m_book.apply(msg);
    if (m_book.liveOrders() > m_stats.maxLive) {
        m_stats.maxLive = m_book.liveOrders();
    }
    checkCrossed();
}

void BookReplay::finish() noexcept {
    if (m_msCount > m_stats.peakPerMs) {
        m_stats.peakPerMs = m_msCount;
        m_stats.peakMsBucket = m_msBucket;
    }
    if (m_secCount > m_stats.peakPerSec) {
        m_stats.peakPerSec = m_secCount;
        m_stats.peakSecBucket = m_secBucket;
    }
}

const ReplayStats& BookReplay::stats() const noexcept {
    return m_stats;
}

const dut::BookBuilder& BookReplay::book() const noexcept {
    return m_book;
}

const util::Histogram& BookReplay::interArrivalNs() const noexcept {
    return m_gap;
}

bool BookReplay::inContinuousSession() const noexcept {
    return m_marketHours && m_trading;
}

void BookReplay::checkReference(OrderId ref, Quantity reduceBy) noexcept {
    const Quantity resting = m_book.restingShares(ref);
    if (resting == 0) {
        ++m_stats.unknownRef;
        return;
    }
    if (reduceBy > resting) {
        ++m_stats.overReduce;
    }
}

void BookReplay::rate(std::uint64_t ts) noexcept {
    if (m_prevTs != 0 && ts >= m_prevTs) {
        m_gap.record(static_cast<std::int64_t>(ts - m_prevTs));
    }
    m_prevTs = ts;

    const std::uint64_t ms = ts / 1'000'000ull;
    if (ms != m_msBucket) {
        if (m_msCount > m_stats.peakPerMs) {
            m_stats.peakPerMs = m_msCount;
            m_stats.peakMsBucket = m_msBucket;
        }
        m_msBucket = ms;
        m_msCount = 0;
    }
    ++m_msCount;

    const std::uint64_t sec = ts / 1'000'000'000ull;
    if (sec != m_secBucket) {
        if (m_secCount > m_stats.peakPerSec) {
            m_stats.peakPerSec = m_secCount;
            m_stats.peakSecBucket = m_secBucket;
        }
        m_secBucket = sec;
        m_secCount = 0;
    }
    ++m_secCount;
}

void BookReplay::checkCrossed() noexcept {
    if (!inContinuousSession()) {
        return;
    }
    const Price bb = m_book.bestBid();
    const Price ba = m_book.bestAsk();
    if (bb != kNoPrice && ba != kNoPrice && bb >= ba) {
        ++m_stats.crossed;
    }
}

}
