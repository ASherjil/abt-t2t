#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "abt/config/BackendTraits.hpp"

#include "DPDK.hpp"

struct Backend {
    using Type                                    = DPDK<DpdkMode::RxTx>;
    static constexpr std::string_view kName       = "dpdk";
    static constexpr std::uint32_t    kMaxTxFrame = 2048;

    static constexpr std::string_view kMlx5LatencyDevargs =
        "txq_mem_algn=0,txqs_min_inline=0,rxq_cqe_comp_en=0";
    static constexpr std::uint16_t    kMlx5NbRxDesc = 64;
    static constexpr std::string_view kSfcLatencyDevargs =
        "perf_profile=low-latency,rx_datapath=ef10,tx_datapath=ef10_simple,stats_update_period_ms=0";
    static constexpr std::uint16_t kSfcNbRxDesc = 64;

    static Type make(const abt::NicSpec& nic) {
        return Type(nic.interface, nic.cpuCore, nic.driver);
    }

    static bool init(Type& b, const abt::NicSpec& nic) {
        if (nic.driver.find("mlx5") != std::string::npos) {
            b.setDevargs(kMlx5LatencyDevargs);
            b.setTxInlineReuse(true);
            b.setNbRxDesc(kMlx5NbRxDesc);
        } else if (nic.driver == "sfc") {
            b.setDevargs(kSfcLatencyDevargs);
            b.setNbRxDesc(kSfcNbRxDesc);
        }
        return b.init(true, false, false, false);
    }
};
