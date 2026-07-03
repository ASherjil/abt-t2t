#pragma once
//
// Types.hpp -- core value types for the limit order book.
//
// Prices are integer ticks (the wire Price(4)/Price(8) integer, or a coarser tick if the
// venue quantises). Quantities are unsigned shares. An OrderId is the exchange-assigned
// order reference number (used when emitting ITCH/OUCH). A Handle is a dense index into
// the book's order pool -- a stable, pointer-free reference used for O(1) cancel/modify.
//
#include <cstdint>
#include <limits>

namespace abt::lob {

using Price    = std::int32_t;    // price in integer ticks
using Quantity = std::uint32_t;   // shares (unsigned)
using OrderId  = std::uint64_t;   // exchange-assigned order reference number
using Handle   = std::uint32_t;   // dense order-pool index (stable book handle)

inline constexpr Handle kNilHandle = std::numeric_limits<Handle>::max();
// Sentinel returned by bestBid()/bestAsk() when that side of the book is empty.
inline constexpr Price  kNoPrice   = std::numeric_limits<Price>::min();

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// A resting order in the book. Intrusive FIFO links (pool indices) give price-time
// priority within a level and O(1) unlink on cancel.
struct Order {
    OrderId  id;      // exchange order reference number
    Price    price;
    Quantity qty;     // remaining shares
    Side     side;
    Handle   prev;    // older order at this level (kNilHandle at head)
    Handle   next;    // newer order at this level (kNilHandle at tail)
};

// Emitted for every execution. The execution price is always the resting order's price
// (price-time priority: the resting order set the price). `restingFilled` means the
// resting order was fully consumed and removed from the book.
struct Trade {
    OrderId  restingId;
    OrderId  aggressorId;
    Price    price;
    Quantity qty;
    Side     aggressorSide;
    bool     restingFilled;
};

// A trade sink that discards everything (for callers that don't need fills).
struct NullSink {
    void onTrade(const Trade&) noexcept {}
};

}  // namespace abt::lob
