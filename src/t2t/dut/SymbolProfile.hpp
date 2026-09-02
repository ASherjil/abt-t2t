#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "t2t/lob/Types.hpp"

namespace abt::dut {

struct SymbolProfile {
    std::string   name;
    Price         refPrice   = 0;
    std::uint32_t peakOrders = 0;
};

[[nodiscard]] std::vector<SymbolProfile> readSymbolProfile(const std::string& path);
[[nodiscard]] bool writeSymbolProfile(const std::string& path, std::span<const SymbolProfile> rows);

}   // namespace abt::dut
