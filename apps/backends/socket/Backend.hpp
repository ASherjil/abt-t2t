#pragma once

#include <cstdint>
#include <string_view>

#include "abt/config/BackendTraits.hpp"

struct Backend {
    using Type = abt::SocketBackend;
    static constexpr std::string_view kName = "socket";
    static constexpr std::uint32_t kMaxTxFrame = 0;

    static Type make(const abt::NicSpec&) {
        return Type{};
    }
    static bool init(Type&, const abt::NicSpec&) {
        return true;
    }
};
