#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/SymbolProfile.hpp"
#include "t2t/lob/Types.hpp"

namespace abt::dut {

enum class BookScope : std::uint8_t {
    All,
    HotOnly,
    ColdOnly
};

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
    std::vector<SymbolProfile> profiles;
    BookScope                  scope  = BookScope::All;
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
    int                              prefetchHotOrders(std::span<const std::byte> msg) const noexcept;

    [[nodiscard]] bool isHot(std::uint16_t locate) const noexcept {
        return ((m_hotBits[locate >> 6] >> (locate & 63u)) & 1u) != 0;
    }

    [[nodiscard]] std::size_t        symbols() const noexcept;
    [[nodiscard]] std::uint64_t      undirected() const noexcept;
    [[nodiscard]] std::uint64_t      rehashes() const noexcept;
    [[nodiscard]] std::uint64_t      reanchors() const noexcept;
    [[nodiscard]] std::uint64_t      rescans() const noexcept;
    [[nodiscard]] std::uint64_t      created() const noexcept;
    [[nodiscard]] std::size_t        profiled() const noexcept;
    [[nodiscard]] const BookBuilder* bookByName(std::string_view name) const noexcept;
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
    struct NameHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    [[nodiscard]] BookConfig configFor(bool hot) noexcept;
    [[nodiscard]] int        hotIndexByName(std::string_view name) const noexcept;
    BookBuilder*             create(std::uint16_t locate, bool hot);
    BookBuilder*             createProfiled(const SymbolProfile& p, bool hot);
    void                     onDirectory(std::span<const std::byte> msg, std::uint16_t locate);

    BookTableConfig m_cfg;

    struct Entry {
        BookBuilder* book = nullptr;
        std::int16_t hot  = static_cast<std::int16_t>(kCold);
    };

    std::pmr::deque<BookBuilder>                                             m_storage;
    std::vector<Entry>                                                       m_books;
    std::array<std::uint64_t, kLocates / 64>                                 m_hotBits{};
    BookBuilder                                                              m_empty;
    std::vector<HotSymbol>                                                   m_hot;
    std::unordered_map<std::string, BookBuilder*, NameHash, std::equal_to<>> m_byName;
    std::uint64_t                                                            m_undirected = 0;
    std::uint64_t                                                            m_rehashes   = 0;
    std::uint64_t                                                            m_reanchors  = 0;
    std::uint64_t                                                            m_rescans    = 0;
    std::uint64_t                                                            m_created    = 0;
    std::uint32_t                                                            m_epoch      = 0;
    std::size_t                                                              m_live       = 0;
};

inline std::uint16_t BookTable::locateOf(std::span<const std::byte> msg) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(msg[1]) << 8) |
                                      std::to_integer<unsigned>(msg[2]));
}

}   // namespace abt::dut
