#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "DPDK.hpp"

#include "abt/sim/SimRunner.hpp"

struct SimBackend {
    using Type = DPDK<DpdkMode::RxTx>;
    static constexpr std::string_view kName = "dpdk";

    static constexpr std::string_view kMlx5LatencyDevargs =
        "txq_mem_algn=0,txqs_min_inline=0,rxq_cqe_comp_en=0";
    static constexpr std::uint16_t kMlx5NbRxDesc = 64;

    static Type make(const abt::SimConfig& cfg) {
        return Type(cfg.transport.interface, cfg.transport.cpuCore, cfg.transport.driver);
    }
    static bool init(Type& nic, const abt::SimConfig& cfg) {
        if (cfg.transport.driver.find("mlx5") != std::string::npos) {
            nic.setDevargs(kMlx5LatencyDevargs);
            nic.setTxInlineReuse(true);
            nic.setNbRxDesc(kMlx5NbRxDesc);
        }
        return nic.init(true, false, false, false);
    }
};
