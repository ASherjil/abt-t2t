#include "t2t/replay/FeedValidator.hpp"

#include "t2t/protocol/Itch50.hpp"

namespace abt::replay {

namespace {

template <class T>
const T* as(std::span<const std::byte> msg) noexcept {
    return msg.size() >= sizeof(T) ? reinterpret_cast<const T*>(msg.data()) : nullptr;
}

}   // namespace

FeedValidator::FeedValidator(const dut::BookTableConfig& cfg)
    : m_books(cfg),
      m_sym(dut::BookTable::kLocates),
      m_trading(dut::BookTable::kLocates, true),
      m_resumedAt(dut::BookTable::kLocates, 0) {
}

void FeedValidator::onMessage(std::span<const std::byte> msg) {
    if (msg.size() < 11) {
        return;
    }
    const char type = static_cast<char>(msg[0]);
    if (type == 'S') {
        const auto* s = as<itch::SystemEvent>(msg);
        if (s == nullptr) {
            return;
        }
        switch (static_cast<itch::SystemEventCode>(s->eventCode)) {
            case itch::SystemEventCode::StartOfMessages:
                m_books.clearAll();
                break;
            case itch::SystemEventCode::StartOfMarketHours:
                m_marketHours = true;
                break;
            case itch::SystemEventCode::EndOfMarketHours:
                m_marketHours = false;
                break;
            default:
                break;
        }
        return;
    }
    const std::uint16_t locate = dut::BookTable::locateOf(msg);
    SymbolStats&        st     = m_sym[locate];
    ++m_messages;
    ++st.messages;

    switch (type) {
        case 'R': {
            if (const auto* r = as<itch::StockDirectory>(msg); r != nullptr) {
                st.name = std::string(r->stock.view());
            }
            break;
        }
        case 'H': {
            if (const auto* h = as<itch::StockTradingAction>(msg); h != nullptr) {
                const bool trading = h->tradingState == itch::TradingState::Trading;
                if (trading && !m_trading[locate]) {
                    std::uint64_t ts = 0;
                    for (std::size_t i = 5; i < 11; ++i) {
                        ts = (ts << 8) | std::to_integer<std::uint64_t>(msg[i]);
                    }
                    m_resumedAt[locate] = ts;
                }
                m_trading[locate] = trading;
            }
            break;
        }
        case 'E': {
            if (const auto* e = as<itch::OrderExecuted>(msg); e != nullptr) {
                checkReference(locate, e->orderRef.value(), e->executedShares.value());
            }
            break;
        }
        case 'C': {
            if (const auto* c = as<itch::OrderExecutedWithPrice>(msg); c != nullptr) {
                checkReference(locate, c->orderRef.value(), c->executedShares.value());
            }
            break;
        }
        case 'X': {
            if (const auto* x = as<itch::OrderCancel>(msg); x != nullptr) {
                checkReference(locate, x->orderRef.value(), x->cancelledShares.value());
            }
            break;
        }
        case 'D': {
            if (const auto* d = as<itch::OrderDelete>(msg); d != nullptr) {
                checkReference(locate, d->orderRef.value(), 0);
            }
            break;
        }
        case 'U': {
            if (const auto* u = as<itch::OrderReplace>(msg); u != nullptr) {
                checkReference(locate, u->origOrderRef.value(), 0);
            }
            break;
        }
        default: {
            break;
        }
    }

    (void)m_books.apply(msg);

    if (type == 'A' || type == 'F' || type == 'U' || type == 'D' || type == 'E' || type == 'C' ||
        type == 'X') {
        const dut::BookBuilder* b = m_books.book(locate);
        if (b != nullptr) {
            const Price bb = b->bestBid();
            const Price ba = b->bestAsk();
            if ((type == 'A' || type == 'F' || type == 'U') && m_marketHours && m_trading[locate] &&
                bb != kNoPrice && ba != kNoPrice && bb >= ba) {
                std::uint64_t ts = 0;
                for (std::size_t i = 5; i < 11; ++i) {
                    ts = (ts << 8) | std::to_integer<std::uint64_t>(msg[i]);
                }
                if (m_resumedAt[locate] != 0 && ts < m_resumedAt[locate] + kResumeGraceNs) {
                    ++st.resumeXing;
                } else if (bb > ba) {
                    ++st.crossed;
                } else {
                    ++st.locked;
                }
            }
            if (b->liveOrders() > st.maxLive) {
                st.maxLive = b->liveOrders();
            }
        }
    }
    if ((m_messages & 0xffffu) == 0) {
        const std::size_t live = m_books.liveOrders();
        if (live > m_maxLive) {
            m_maxLive = live;
        }
    }
}

void FeedValidator::checkReference(std::uint16_t locate, OrderId ref, Quantity reduceBy) noexcept {
    const dut::BookBuilder* b       = m_books.book(locate);
    const Quantity          resting = b == nullptr ? 0 : b->restingShares(ref);
    if (resting == 0) {
        ++m_sym[locate].unknownRef;
        return;
    }
    if (reduceBy > resting) {
        ++m_sym[locate].overReduce;
    }
}

FeedTotals FeedValidator::totals() const noexcept {
    FeedTotals t{};
    t.messages = m_messages;
    t.maxLive  = m_maxLive;
    for (const SymbolStats& s : m_sym) {
        if (s.messages == 0) {
            continue;
        }
        ++t.symbols;
        t.unknownRef += s.unknownRef;
        t.overReduce += s.overReduce;
        t.crossed += s.crossed;
        t.locked += s.locked;
        t.resumeXing += s.resumeXing;
    }
    m_books.forEachBook([&](std::uint16_t, const dut::BookBuilder& b) {
        t.outOfBand += b.outOfBandAdds();
        t.reanchors += b.reanchors();
        if (b.anchored() && b.tickWire() == 1) {
            ++t.subDollar;
        }
    });
    return t;
}

const std::vector<SymbolStats>& FeedValidator::perSymbol() const noexcept {
    return m_sym;
}

const dut::BookTable& FeedValidator::books() const noexcept {
    return m_books;
}

}   // namespace abt::replay
