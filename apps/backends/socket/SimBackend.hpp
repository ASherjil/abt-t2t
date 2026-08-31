#pragma once

#include <string_view>

#include "abt/sim/SimRunner.hpp"

struct SimBackend {
    using Type = abt::SocketBackend;
    static constexpr std::string_view kName = "socket";

    static Type make(const abt::SimConfig&) {
        return Type{};
    }
    static bool init(Type&, const abt::SimConfig&) {
        return true;
    }
};
