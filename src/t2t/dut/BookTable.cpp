#include "t2t/dut/BookTable.hpp"

#include <bit>

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
        m_hotBits[locate >> 6] |= std::uint64_t{1} << (locate & 63u);
        if (m_books[locate].book == nullptr) {
            create(locate, true);
        }
        m_hot[i].book = m_books[locate].book;
    }
    m_byName.reserve(cfg.profiles.size());
    for (const SymbolProfile& p : cfg.profiles) {
        if (p.name.empty() || m_byName.contains(p.name)) {
            continue;
        }
        const int h = hotIndexByName(p.name);
        if ((cfg.scope == BookScope::HotOnly && h == kCold) ||
            (cfg.scope == BookScope::ColdOnly && h != kCold)) {
            continue;
        }
        BookBuilder* b = createProfiled(p, h != kCold);
        m_byName.emplace(p.name, b);
        if (h != kCold && m_hot[static_cast<std::size_t>(h)].book == nullptr) {
            m_hot[static_cast<std::size_t>(h)].book = b;
        }
    }
}

int BookTable::hotIndexByName(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < m_hot.size(); ++i) {
        if (m_hot[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return kCold;
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
        if (m_cfg.scope == BookScope::HotOnly && e.hot == kCold) {
            return kCold;
        }
        if (m_cfg.scope == BookScope::ColdOnly && e.hot != kCold) {
            return e.hot;
        }
        ++m_undirected;
        create(locate, e.hot != kCold);
    }
    if (type == 'H') [[unlikely]] {
        const int h = e.hot;
        if (h != kCold && msg.size() >= sizeof(itch::StockTradingAction)) {
            const auto* a = reinterpret_cast<const itch::StockTradingAction*>(msg.data());
            m_hot[static_cast<std::size_t>(h)].trading = a->tradingState == itch::TradingState::Trading;
        }
        return h;
    }
    BookBuilder& b = *e.book;
    if (b.epoch() != m_epoch) [[unlikely]] {
        b.clear();
        b.setEpoch(m_epoch);
    }
    const std::size_t before = b.liveOrders();
    b.apply(msg);
    m_live += b.liveOrders() - before;
    return e.hot;
}

void BookTable::onDirectory(std::span<const std::byte> msg, std::uint16_t locate) {
    if (msg.size() < sizeof(itch::StockDirectory)) {
        return;
    }
    const auto*            r    = reinterpret_cast<const itch::StockDirectory*>(msg.data());
    const std::string_view name = r->stock.view();
    Entry&                 e    = m_books[locate];
    if (e.book == nullptr) {
        if (const auto it = m_byName.find(name); it != m_byName.end()) {
            e.book = it->second;
        }
    }
    if (e.hot == kCold) {
        for (std::size_t i = 0; i < m_hot.size(); ++i) {
            if (!m_hot[i].resolved && m_hot[i].name == name) {
                m_hot[i].resolved = true;
                m_hot[i].locate   = locate;
                e.hot             = static_cast<std::int16_t>(i);
                m_hotBits[locate >> 6] |= std::uint64_t{1} << (locate & 63u);
                if (m_cfg.scope == BookScope::ColdOnly) {
                    return;
                }
                if (e.book == nullptr) {
                    create(locate, true);
                }
                m_hot[i].book = e.book;
                return;
            }
        }
    }
    if (e.book == nullptr && m_cfg.scope != BookScope::HotOnly) {
        create(locate, false);
    }
}

BookConfig BookTable::configFor(bool hot) noexcept {
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
    bc.reanchors         = &m_reanchors;
    bc.rescans           = &m_rescans;
    return bc;
}

BookBuilder* BookTable::create(std::uint16_t locate, bool hot) {
    m_storage.emplace_back(configFor(hot));
    ++m_created;
    m_books[locate].book = &m_storage.back();
    return m_books[locate].book;
}

BookBuilder* BookTable::createProfiled(const SymbolProfile& p, bool hot) {
    BookConfig bc   = configFor(hot);
    const auto want = static_cast<std::size_t>(p.peakOrders) * 2;
    if (want > bc.maxOrders) {
        bc.maxOrders = std::bit_ceil(want);
    }
    if (p.refPrice > 0) {
        bc.anchorPrice = p.refPrice;
    }
    m_storage.emplace_back(bc);
    ++m_created;
    return &m_storage.back();
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

int BookTable::prefetchHotOrders(std::span<const std::byte> msg) const noexcept {
    if (msg.size() < sizeof(itch::OrderDelete)) {
        return kCold;
    }
    const std::uint16_t locate = locateOf(msg);
    if (!isHot(locate)) {
        return kCold;
    }
    const Entry& e    = m_books[locate];
    const char   type = static_cast<char>(msg[0]);
    switch (type) {
        case 'A':
        case 'F':
        case 'E':
        case 'C':
        case 'X':
        case 'D':
        case 'U':
            break;
        default:
            return e.hot;
    }
    if (e.book == nullptr) {
        return e.hot;
    }
    const auto* d = reinterpret_cast<const itch::OrderDelete*>(msg.data());
    e.book->prefetchOrder(d->orderRef.value());
    if (type == 'U' && msg.size() >= sizeof(itch::OrderReplace)) {
        const auto* u = reinterpret_cast<const itch::OrderReplace*>(msg.data());
        e.book->prefetchOrder(u->newOrderRef.value());
    }
    return e.hot;
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

std::uint64_t BookTable::reanchors() const noexcept {
    return m_reanchors;
}

std::uint64_t BookTable::rescans() const noexcept {
    return m_rescans;
}

std::uint64_t BookTable::created() const noexcept {
    return m_created;
}

std::size_t BookTable::profiled() const noexcept {
    return m_byName.size();
}

const BookBuilder* BookTable::bookByName(std::string_view name) const noexcept {
    const auto it = m_byName.find(name);
    return it == m_byName.end() ? nullptr : it->second;
}

std::size_t BookTable::footprintBytes() const noexcept {
    std::size_t n = m_books.capacity() * sizeof(Entry);
    for (const BookBuilder& b : m_storage) {
        n += b.footprintBytes();
    }
    return n;
}

std::size_t BookTable::liveOrders() const noexcept {
    return m_live;
}

std::size_t BookTable::bookCapacity(std::uint16_t locate) const noexcept {
    const BookBuilder* b = m_books[locate].book;
    return b == nullptr ? 0 : b->orderCapacity();
}

void BookTable::clearAll() noexcept {
    ++m_epoch;
    m_live = 0;
    for (HotSymbol& h : m_hot) {
        h.trading = true;
        if (h.book != nullptr) {
            h.book->clear();
            h.book->setEpoch(m_epoch);
        }
    }
}

}   // namespace abt::dut
