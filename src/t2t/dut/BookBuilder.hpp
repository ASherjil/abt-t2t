#pragma once
//
// Feed-driven L2 order book (DUT side): applies the ITCH 5.0 add/execute/cancel/delete/replace
// stream and maintains top-of-book. It is a book *builder* — the mirror of the exchange sim's
// matching engine — and is the market-data view the DUT's strategy reacts to. Flat per-side
// price-level arrays (O(1) apply) + an order-ref map + incrementally tracked best bid/ask.
//

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include "t2t/lob/Types.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/util/DivBy.hpp"
#include "t2t/util/FlatHashMap.hpp"
#include "t2t/util/Scan.hpp"

namespace abt::dut {

struct BookConfig {
    Price                      tickWire     = 1;
    std::size_t                bandTicks    = 2048;
    double                     bandFraction = 0.0;
    std::size_t                maxOrders    = 1u << 10;
    OrderId                    ownRefMin    = 0;
    std::pmr::memory_resource* memory       = nullptr;
    std::uint64_t*             rehashes     = nullptr;
};

class BookBuilder {
public:
    // maxOrders sizes the order-ref map's initial capacity (it grows if exceeded). ITCH order refs
    // are >= 1, so 0 is reserved as the map's empty sentinel.
    BookBuilder(Price minPrice, Price maxPrice, Price tickWire, std::size_t maxOrders = 1u << 12,
                OrderId ownRefMin = 0);
    explicit BookBuilder(const BookConfig& cfg);

    void apply(std::span<const std::byte> itchMessage);
    void clear() noexcept;

    [[nodiscard]] Price         bestBid() const noexcept;
    [[nodiscard]] Price         bestAsk() const noexcept;
    [[nodiscard]] Quantity      sizeAt(Side side, Price price) const noexcept;
    [[nodiscard]] Quantity      restingShares(OrderId ref) const noexcept;
    [[nodiscard]] std::size_t   liveOrders() const noexcept;
    [[nodiscard]] std::size_t   orderCapacity() const noexcept;
    [[nodiscard]] std::size_t   ownOrders() const noexcept;
    [[nodiscard]] bool          anchored() const noexcept;
    [[nodiscard]] Price         bandLow() const noexcept;
    [[nodiscard]] Price         bandHigh() const noexcept;
    [[nodiscard]] std::uint64_t outOfBandAdds() const noexcept;

private:
    struct Resting {
        Price    price;
        Quantity shares;
        Side     side;
        bool     own;
    };

    void onAddOrder(const itch::AddOrder& msg);
    void onOrderReplace(const itch::OrderReplace& msg);
    void reduceOrder(OrderId ref, Quantity by);
    void removeOrder(OrderId ref);
    void addShares(Side side, Price price, Quantity shares) noexcept;
    void removeShares(Side side, Price price, Quantity shares) noexcept;

    [[nodiscard]] bool        inBand(Price price) const noexcept;
    [[nodiscard]] std::size_t index(Price price) const noexcept;
    void                      anchor(Price price);
    void                      rescanBestBid() noexcept;
    void                      rescanBestAsk() noexcept;

    Price         m_minPrice;
    Price         m_maxPrice;
    Price         m_tickWire;
    OrderId       m_ownRefMin;
    std::size_t   m_own = 0;
    util::DivBy   m_tickDiv;   // price-offset / tickWire without a hardware divide
    Price         m_bestBid      = kNoPrice;
    Price         m_bestAsk      = kNoPrice;
    std::size_t   m_bandTicks    = 0;
    double        m_bandFraction = 0.0;
    bool          m_anchored     = true;
    std::uint64_t m_oob          = 0;

    std::pmr::vector<Quantity>          m_bidSize;
    std::pmr::vector<Quantity>          m_askSize;
    util::FlatHashMap<OrderId, Resting> m_orders;
};

}   // namespace abt::dut
