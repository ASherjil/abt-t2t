#pragma once
//
// Standalone value config for the matching venue.
//

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "t2t/lob/Types.hpp"

namespace abt {

struct ExchangeConfig {
    std::vector<std::string>   symbols       = {"AAPL"};
    std::vector<std::uint16_t> locates       = {1};
    std::string                session       = "SIM0000001";
    Price                      minTick       = 1;
    Price                      maxTick       = 100000;
    std::uint32_t              wirePerTick   = 100;
    std::size_t                mdMaxPayload  = 1400;
    OrderId                    firstOrderRef = 1;
    std::size_t                liveReserve   = 1u << 12;
};

}   // namespace abt
