#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/lob/Types.hpp"

namespace abt::dut {

struct BookTableConfig {
    Price                      tickWire      = 100;
    Price                      subDollarTick = 1;
    std::size_t                coldBandTicks = 2048;
    std::size_t                hotBandTicks  = 8192;
    std::size_t                maxBandTicks  = 1u << 16;
    double                     bandFraction  = 0.10;
    std::size_t                coldMapSlots  = 1024;
    std::size_t                hotMapSlots   = 1u << 16;
    OrderId                    ownRefMin     = 0;
    std::vector<std::string>   hotSymbols;
    std::vector<std::uint16_t> hotLocates;
    std::pmr::memory_resource* memory = nullptr;
};

class BookBuilder;

struct HotSymbol {
    std::string   name;
    BookBuilder*  book     = nullptr;
    std::uint16_t locate   = 0;
    bool          resolved = false;
    bool          trading  = true;
};

class BookTable {
public:
    static constexpr int         kCold    = -1;
    static constexpr std::size_t kLocates = 1u << 16;

    explicit BookTable(const BookTableConfig& cfg);

    int apply(std::span<const std::byte> msg);

    [[nodiscard]] const BookBuilder* book(std::uint16_t locate) const noexcept;
    [[nodiscard]] const BookBuilder& hotBook(std::size_t idx) const noexcept;
    [[nodiscard]] const HotSymbol&   hot(std::size_t idx) const noexcept;
    [[nodiscard]] std::size_t        hotCount() const noexcept;
    [[nodiscard]] int                hotIndexOf(std::uint16_t locate) const noexcept;
    [[nodiscard]] std::size_t        symbols() const noexcept;
    [[nodiscard]] std::uint64_t      undirected() const noexcept;
    [[nodiscard]] std::uint64_t      rehashes() const noexcept;
    [[nodiscard]] std::size_t        footprintBytes() const noexcept;
    [[nodiscard]] std::size_t        liveOrders() const noexcept;
    [[nodiscard]] std::size_t        bookCapacity(std::uint16_t locate) const noexcept;

    void clearAll() noexcept;

    template <class Fn>
    void forEachBook(Fn fn) const {
        for (std::size_t l = 0; l < kLocates; ++l) {
            if (m_books[l].book != nullptr) {
                fn(static_cast<std::uint16_t>(l), *m_books[l].book);
            }
        }
    }

    [[nodiscard]] static std::uint16_t locateOf(std::span<const std::byte> msg) noexcept;

private:
    BookBuilder* create(std::uint16_t locate, bool hot);
    void         onDirectory(std::span<const std::byte> msg, std::uint16_t locate);

    BookTableConfig m_cfg;

    struct Entry {
        BookBuilder* book = nullptr;
        std::int16_t hot  = static_cast<std::int16_t>(kCold);
    };

    std::pmr::deque<BookBuilder> m_storage;
    std::vector<Entry>           m_books;
    BookBuilder                  m_empty;
    std::vector<HotSymbol>       m_hot;
    std::uint64_t                m_undirected = 0;
    std::uint64_t                m_rehashes   = 0;
};

inline std::uint16_t BookTable::locateOf(std::span<const std::byte> msg) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                      std::to_integer<unsigned>(msg[2]));
}

}   // namespace abt::dut
