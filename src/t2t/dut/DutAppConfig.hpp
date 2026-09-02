#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "t2t/dut/DutSession.hpp"
#include "t2t/dut/QuoterStrategy.hpp"
#include "t2t/protocol/UdpFramer.hpp"

namespace abt::dut {

struct MeasureConfig {
    int           histogramCore = -1;
    std::size_t   queueCapacity = 1u << 16;
    int           sigFigs       = 3;
    std::string   logFile;
    std::uint32_t flushIntervalS = 60;
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
    std::string    interface = "sfc0";
    std::string    driver    = "sfc";
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

}   // namespace abt::dut
