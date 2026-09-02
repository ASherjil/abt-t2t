#include "t2t/dut/BookTable.hpp"

#include "t2t/protocol/Itch50.hpp"

namespace abt::dut {

BookTable::BookTable(const BookTableConfig& cfg)
    : m_cfg(cfg),
      m_storage(cfg.memory == nullptr ? std::pmr::get_default_resource() : cfg.memory),
      m_books(kLocates),
      m_empty(BookConfig{.tickWire = cfg.tickWire, .bandTicks = 1, .maxOrders = 16}) {
    m_hot.reserve(cfg.hotSymbols.size());
    for (const std::string& name : cfg.hotSymbols) {
        m_hot.push_back(HotSymbol{.name = name});
    }
    for (std::size_t i = 0; i < m_hot.size() && i < cfg.hotLocates.size(); ++i) {
        const std::uint16_t locate = cfg.hotLocates[i];
        m_hot[i].resolved          = true;
        m_hot[i].locate            = locate;
        m_books[locate].hot        = static_cast<std::int16_t>(i);
        if (m_books[locate].book == nullptr) {
            create(locate, true);
        }
        m_hot[i].book = m_books[locate].book;
    }
}

int BookTable::apply(std::span<const std::byte> msg) {
    const std::uint16_t locate = locateOf(msg);
    const char          type   = static_cast<char>(msg[0]);
    if (type == 'R') [[unlikely]] {
        onDirectory(msg, locate);
        return m_books[locate].hot;
    }
    Entry& e = m_books[locate];
    if (e.book == nullptr) [[unlikely]] {
        ++m_undirected;
        create(locate, false);
    }
    if (type == 'H') [[unlikely]] {
        const int h = e.hot;
        if (h != kCold && msg.size() >= sizeof(itch::StockTradingAction)) {
            const auto* a = reinterpret_cast<const itch::StockTradingAction*>(msg.data());
            m_hot[static_cast<std::size_t>(h)].trading = a->tradingState == itch::TradingState::Trading;
        }
        return h;
    }
    e.book->apply(msg);
    return e.hot;
}

void BookTable::onDirectory(std::span<const std::byte> msg, std::uint16_t locate) {
    if (msg.size() < sizeof(itch::StockDirectory)) {
        return;
    }
    const auto* r = reinterpret_cast<const itch::StockDirectory*>(msg.data());
    if (m_books[locate].hot == kCold) {
        const std::string_view name = r->stock.view();
        for (std::size_t i = 0; i < m_hot.size(); ++i) {
            if (!m_hot[i].resolved && m_hot[i].name == name) {
                m_hot[i].resolved   = true;
                m_hot[i].locate     = locate;
                m_books[locate].hot = static_cast<std::int16_t>(i);
                if (m_books[locate].book == nullptr) {
                    create(locate, true);
                }
                m_hot[i].book = m_books[locate].book;
                return;
            }
        }
    }
    if (m_books[locate].book == nullptr) {
        create(locate, false);
    }
}

BookBuilder* BookTable::create(std::uint16_t locate, bool hot) {
    BookConfig bc{};
    bc.tickWire          = m_cfg.tickWire;
    bc.subDollarTickWire = m_cfg.subDollarTick;
    bc.bandTicks         = hot ? m_cfg.hotBandTicks : m_cfg.coldBandTicks;
    bc.bandFraction      = m_cfg.bandFraction;
    bc.maxBandTicks      = m_cfg.maxBandTicks;
    bc.maxOrders         = hot ? m_cfg.hotMapSlots : m_cfg.coldMapSlots;
    bc.ownRefMin         = m_cfg.ownRefMin;
    bc.memory            = m_cfg.memory;
    bc.rehashes          = &m_rehashes;
    m_storage.emplace_back(bc);
    m_books[locate].book = &m_storage.back();
    return m_books[locate].book;
}

const BookBuilder* BookTable::book(std::uint16_t locate) const noexcept {
    return m_books[locate].book;
}

const BookBuilder& BookTable::hotBook(std::size_t idx) const noexcept {
    const BookBuilder* b = m_hot[idx].book;
    return b == nullptr ? m_empty : *b;
}

const HotSymbol& BookTable::hot(std::size_t idx) const noexcept {
    return m_hot[idx];
}

std::size_t BookTable::hotCount() const noexcept {
    return m_hot.size();
}

int BookTable::hotIndexOf(std::uint16_t locate) const noexcept {
    return m_books[locate].hot;
}

std::size_t BookTable::symbols() const noexcept {
    return m_storage.size();
}

std::uint64_t BookTable::undirected() const noexcept {
    return m_undirected;
}

std::uint64_t BookTable::rehashes() const noexcept {
    return m_rehashes;
}

std::size_t BookTable::footprintBytes() const noexcept {
    std::size_t n = m_books.capacity() * sizeof(Entry);
    for (const BookBuilder& b : m_storage) {
        n += b.footprintBytes();
    }
    return n;
}

std::size_t BookTable::liveOrders() const noexcept {
    std::size_t n = 0;
    for (const BookBuilder& b : m_storage) {
        n += b.liveOrders();
    }
    return n;
}

std::size_t BookTable::bookCapacity(std::uint16_t locate) const noexcept {
    const BookBuilder* b = m_books[locate].book;
    return b == nullptr ? 0 : b->orderCapacity();
}

void BookTable::clearAll() noexcept {
    for (BookBuilder& b : m_storage) {
        b.clear();
    }
    for (HotSymbol& h : m_hot) {
        h.trading = true;
    }
}

}   // namespace abt::dut
