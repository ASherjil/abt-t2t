#pragma once
//
// Standalone value configs for the matching venue and the synthetic flow generator.
//

#include <cstdint>
#include <string>

#include "abt/lob/Types.hpp"

namespace abt {

struct ExchangeConfig {
    std::string   symbol      = "AAPL";
    std::uint16_t stockLocate = 1;
    std::string   session     = "SIM0000001";
    Price         minTick     = 1;
    Price         maxTick     = 100000;
    std::uint32_t wirePerTick = 100;
};

struct FlowConfig {
    Price          midTick    = 5200;
    Price          halfSpread = 1;
    Price          depthTicks = 20;
    Quantity       minQty     = 10;
    Quantity       maxQty     = 500;
    std::uint32_t  cancelPct  = 30;
    std::uint32_t  crossPct   = 15;
    std::uint64_t  seed       = 0x9E3779B97F4A7C15ull;
};

}
