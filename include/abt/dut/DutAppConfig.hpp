#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "abt/dut/DutSession.hpp"
#include "abt/dut/QuoterStrategy.hpp"
#include "abt/protocol/UdpFramer.hpp"

namespace abt::dut {

struct MeasureConfig {
    int         histogramCore = -1;
    std::size_t queueCapacity = 1u << 16;
    int         sigFigs       = 3;
};

struct DutSocketConfig {
    std::string   oeHost     = "127.0.0.1";
    std::uint16_t oePort     = 5001;
    std::string   mdBindHost = "127.0.0.1";
    std::uint16_t mdPort     = 5002;
    std::string   session    = "SIM0000001";
    std::string   username   = "DUT001";
};

struct DutTransportConfig {
    std::string    mode      = "socket";
    std::string    interface = "sfc0";
    int            cpuCore   = 5;
    net::Endpoints marketData{};
    net::Endpoints orderEntry{};
};

struct DutAppConfig {
    DutConfig          session;
    QuoterConfig       quoter;
    MeasureConfig      measure;
    DutSocketConfig    socket;
    DutTransportConfig transport;
};

[[nodiscard]] DutAppConfig loadDutConfig(const std::string& path);

}
