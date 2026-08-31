#pragma once

#include <string_view>

#include "Verbs.hpp"

#include "abt/sim/SimRunner.hpp"

struct SimBackend {
    using Type = Verbs<VerbsMode::RxTx>;
    static constexpr std::string_view kName = "verbs";

    static Type make(const abt::SimConfig& cfg) {
        return Type(cfg.transport.interface);
    }
    static bool init(Type& nic, const abt::SimConfig&) {
        return nic.init();
    }
};
