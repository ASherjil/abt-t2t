#pragma once
//
// Feed-driven L2 order book (DUT side): applies the ITCH 5.0 add/execute/cancel/delete/replace
// stream and maintains top-of-book. It is a book *builder* — the mirror of the exchange sim's
// matching engine — and is the market-data view the DUT's strategy reacts to. Flat per-side
// price-level arrays (O(1) apply) + an order-ref map + incrementally tracked best bid/ask.
//

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "abt/lob/Types.hpp"
#include "abt/protocol/Itch50.hpp"
#include "abt/util/FlatHashMap.hpp"

namespace abt::dut {

class BookBuilder {
public:
    // maxOrders sizes the order-ref map's initial capacity (it grows if exceeded). ITCH order refs
    // are >= 1, so 0 is reserved as the map's empty sentinel.
    BookBuilder(Price minPrice, Price maxPrice, Price tickWire, std::size_t maxOrders = 1u << 12);

    void apply(std::span<const std::byte> itchMessage);

    [[nodiscard]] Price bestBid() const noexcept;
    [[nodiscard]] Price bestAsk() const noexcept;
    [[nodiscard]] Quantity sizeAt(Side side, Price price) const noexcept;
    [[nodiscard]] std::size_t liveOrders() const noexcept;

private:
    struct Resting {
        Price    price;
        Quantity shares;
        Side     side;
    };

    void onAddOrder(const itch::AddOrder& msg);
    void onOrderReplace(const itch::OrderReplace& msg);
    void reduceOrder(OrderId ref, Quantity by);
    void removeOrder(OrderId ref);
    void addShares(Side side, Price price, Quantity shares) noexcept;
    void removeShares(Side side, Price price, Quantity shares) noexcept;

    [[nodiscard]] bool inBand(Price price) const noexcept;
    [[nodiscard]] std::size_t index(Price price) const noexcept;
    void rescanBestBid() noexcept;
    void rescanBestAsk() noexcept;

    Price m_minPrice;
    Price m_maxPrice;
    Price m_tickWire;
    Price m_bestBid = kNoPrice;
    Price m_bestAsk = kNoPrice;

    std::vector<Quantity>              m_bidSize;
    std::vector<Quantity>              m_askSize;
    util::FlatHashMap<OrderId, Resting> m_orders;
};

}
