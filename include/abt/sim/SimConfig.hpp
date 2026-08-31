#pragma once
//
// Aggregate runtime configuration for the exchange simulator, loaded from a TOML file.
//

#include <cstddef>
#include <cstdint>
#include <string>

#include "abt/protocol/UdpFramer.hpp"
#include "abt/sim/EngineConfig.hpp"
#include "abt/sim/MarketReplay.hpp"

namespace abt {

struct SocketConfig {
    std::uint16_t oePort = 5001;
    std::string   mdHost = "127.0.0.1";
    std::uint16_t mdPort = 5002;
};

struct TransportConfig {
    std::string    interface = "cx0";
    std::string    driver    = "mlx5_core";
    int            cpuCore   = 2;
    net::Endpoints marketData{};
    net::Endpoints orderEntry{};
};

struct SimConfig {
    ExchangeConfig venue;
    FlowConfig     flow;
    SocketConfig   socket;
    TransportConfig     transport;
    ReplayConfig   replay;
    std::size_t    warmupSteps    = 200;
    std::uint64_t  tickIntervalNs = 100000;
};

[[nodiscard]] SimConfig loadConfig(const std::string& path);

}
