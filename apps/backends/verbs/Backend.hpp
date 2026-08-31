#pragma once

#include <string_view>

#include "Verbs.hpp"

#include "abt/config/BackendTraits.hpp"

struct Backend {
    using Type = Verbs<VerbsMode::RxTx>;
    static constexpr std::string_view kName = "verbs";

    static Type make(const abt::NicSpec& nic) {
        return Type(nic.interface);
    }
    static bool init(Type& b, const abt::NicSpec&) {
        return b.init();
    }
};
