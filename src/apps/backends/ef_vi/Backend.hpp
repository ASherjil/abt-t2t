#pragma once

#include <cstdint>
#include <string_view>

#include "t2t/config/BackendTraits.hpp"

#include "EtherFabricVirtualInterface.hpp"

struct Backend {
    static constexpr std::uint16_t kNbRxBufs    = 256;
    static constexpr std::uint16_t kNbTxBufs    = 8;
    static constexpr std::uint32_t kBufSize     = 2048;
    static constexpr unsigned      kCtThreshold = 64;
    static constexpr bool          kUseCtpio    = true;

    using Type = EtherFabricVirtualInterface<EtherFabricMode::RxTx, kNbRxBufs, kNbTxBufs, kBufSize,
                                             kCtThreshold, kUseCtpio>;
    static constexpr std::string_view kName       = "ef_vi";
    static constexpr std::uint32_t    kMaxTxFrame = kBufSize;

    static Type make(const abt::NicSpec& nic) {
        return Type(nic.interface);
    }

    static bool init(Type& b, const abt::NicSpec&) {
        return b.init();
    }
};
