#pragma once
//
// Core value types for the limit order book: Price/Quantity/OrderId/Handle, Order, Trade.
//

#include <cstdint>
#include <limits>

namespace abt::lob {

using Price    = std::int32_t;
using Quantity = std::uint32_t;
using OrderId  = std::uint64_t;
using Handle   = std::uint32_t;

inline constexpr Handle kNilHandle = std::numeric_limits<Handle>::max();
inline constexpr Price  kNoPrice   = std::numeric_limits<Price>::min();

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

struct Order {
    OrderId  id;
    Price    price;
    Quantity qty;
    Side     side;
    Handle   prev;
    Handle   next;
};

struct Trade {
    OrderId  restingId;
    OrderId  aggressorId;
    Price    price;
    Quantity qty;
    Side     aggressorSide;
    bool     restingFilled;
};

struct NullSink {
    void onTrade(const Trade&) noexcept {}
};

}
