#pragma once

#include <cstdint>
#include <string_view>

#include "abt/config/BackendTraits.hpp"

#include "Verbs.hpp"

struct Backend {
    static constexpr std::uint16_t kSqDepth     = 256;
    static constexpr std::uint16_t kRqDepth     = 8192;
    static constexpr std::uint16_t kSignalEvery = 64;
    static constexpr std::uint16_t kMaxInline   = 512;
    static constexpr std::uint16_t kMaxFrame    = 2048;

    using Type = Verbs<VerbsMode::RxTx, 1, kSqDepth, kRqDepth, kSignalEvery, kMaxInline, kMaxFrame>;
    static constexpr std::string_view kName       = "verbs";
    static constexpr std::uint32_t    kMaxTxFrame = kMaxInline;

    static Type make(const abt::NicSpec& nic) {
        return Type(nic.interface);
    }

    static bool init(Type& b, const abt::NicSpec&) {
        return b.init();
    }
};
