#pragma once
//
// The market-making contract between a strategy and the DUT's order manager. The strategy looks at
// the book and its own account and returns the two-sided quotes it wants *resting*; the order
// manager reconciles those targets against what is actually on the wire (new / replace / cancel).
// Expressing intent as desired state rather than individual orders is how real quoting engines are
// structured — the strategy decides prices, the OMS owns the order lifecycle.
//

#include <cstdint>

#include "abt/lob/Types.hpp"

namespace abt::dut {

// The DUT's own trading state, as the strategy sees it.
struct Account {
    std::int64_t position = 0;   // signed inventory in shares (+ long, - short)
};

// The quotes the strategy wants resting after a market-data update. A side with quote == false
// means "pull that side". Prices are wire prices already on the tick grid.
struct QuoteTargets {
    bool     quoteBid = false;
    Price    bidPrice = 0;
    Quantity bidQty   = 0;
    bool     quoteAsk = false;
    Price    askPrice = 0;
    Quantity askQty   = 0;
};

}
