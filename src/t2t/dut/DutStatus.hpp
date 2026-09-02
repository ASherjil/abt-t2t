#pragma once

#include <cstdint>

#include "t2t/lob/Types.hpp"

namespace abt::dut {

struct DutStatus {
    std::uint64_t elapsedNs = 0;
    std::uint64_t packets   = 0;
    std::uint64_t seq       = 0;
    std::uint64_t gaps      = 0;
    std::uint64_t live      = 0;
    std::uint64_t enters    = 0;
    std::uint64_t replaces  = 0;
    std::uint64_t cancels   = 0;
    std::uint64_t accepts   = 0;
    std::uint64_t fills     = 0;
    std::uint64_t rejects   = 0;
    std::int64_t  position  = 0;
    std::uint32_t sent      = 0;
    Price         bid       = kNoPrice;
    Price         ask       = kNoPrice;
    bool          feedValid = true;
};

void printDutStatus(const DutStatus& s);

}   // namespace abt::dut
