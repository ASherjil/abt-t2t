#pragma once

#include <cstdint>
#include <string_view>

#include "abt/protocol/EthIpUdp.hpp"

namespace abt::config {

[[nodiscard]] net::MacAddr parseMac(std::string_view s) noexcept;
[[nodiscard]] std::uint32_t parseIp(std::string_view s) noexcept;

}
