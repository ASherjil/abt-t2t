#include <cstdint>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include "t2t/config/NetParse.hpp"
#include "t2t/dut/DutAppConfig.hpp"
#include "t2t/dut/SymbolProfile.hpp"

namespace abt::dut {

DutAppConfig loadDutConfig(const std::string& path) {
    DutAppConfig c{};

    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        fmt::print(stderr, "dut config: cannot parse {}: {}\n", path, e.description());
        std::exit(1);
    }

    if (const toml::array* syms = t["venue"]["symbols"].as_array(); syms != nullptr) {
        for (const toml::node& n : *syms) {
            if (const auto v = n.value<std::string>(); v.has_value()) {
                c.session.symbols.push_back(*v);
            }
        }
    }
    if (const toml::array* locs = t["venue"]["locates"].as_array(); locs != nullptr) {
        for (const toml::node& n : *locs) {
            if (const auto v = n.value<std::int64_t>(); v.has_value()) {
                c.session.locates.push_back(static_cast<std::uint16_t>(*v));
            }
        }
    }
    c.session.tickWire        = t["venue"]["tick_wire"].value_or(c.session.tickWire);
    c.session.coldBandTicks   = t["venue"]["cold_band_ticks"].value_or(c.session.coldBandTicks);
    c.session.hotBandTicks    = t["venue"]["hot_band_ticks"].value_or(c.session.hotBandTicks);
    c.session.bandFraction    = t["venue"]["band_fraction"].value_or(c.session.bandFraction);
    c.session.coldMapSlots    = t["venue"]["cold_map_slots"].value_or(c.session.coldMapSlots);
    c.session.hotMapSlots     = t["venue"]["hot_map_slots"].value_or(c.session.hotMapSlots);
    c.session.arenaMb         = t["venue"]["arena_mb"].value_or(c.session.arenaMb);
    c.session.firstUserRef    = t["venue"]["first_user_ref"].value_or(c.session.firstUserRef);
    c.session.marketHoursOnly = t["venue"]["market_hours_only"].value_or(c.session.marketHoursOnly);
    c.session.ownRefMin       = static_cast<OrderId>(t["venue"]["own_ref_min"].value_or(std::int64_t{0}));
    if (const std::string profile = t["venue"]["profile"].value_or(std::string{}); !profile.empty()) {
        c.session.profiles = readSymbolProfile(profile);
        if (c.session.profiles.empty()) {
            fmt::print(stderr, "dut config: symbol profile {} missing or empty; books anchor on the feed\n",
                       profile);
        }
    }

    c.quoter.tickWire         = c.session.tickWire;
    c.quoter.halfSpreadTicks  = t["quoter"]["half_spread_ticks"].value_or(c.quoter.halfSpreadTicks);
    c.quoter.quoteQty         = t["quoter"]["quote_qty"].value_or(c.quoter.quoteQty);
    c.quoter.skewTicksPerUnit = t["quoter"]["skew_ticks_per_unit"].value_or(c.quoter.skewTicksPerUnit);

    c.measure.histogramCore  = t["measure"]["histogram_core"].value_or(c.measure.histogramCore);
    c.measure.queueCapacity  = t["measure"]["queue_capacity"].value_or(c.measure.queueCapacity);
    c.measure.sigFigs        = t["measure"]["sig_figs"].value_or(c.measure.sigFigs);
    c.measure.logFile        = t["measure"]["log_file"].value_or(c.measure.logFile);
    c.measure.flushIntervalS = t["measure"]["flush_interval_s"].value_or(c.measure.flushIntervalS);
    c.session.queueCapacity  = c.measure.queueCapacity;
    c.session.sigFigs        = c.measure.sigFigs;

    c.socket.oeHost     = t["socket"]["oe_host"].value_or(c.socket.oeHost);
    c.socket.oePort     = t["socket"]["oe_port"].value_or(c.socket.oePort);
    c.socket.mdBindHost = t["socket"]["md_bind_host"].value_or(c.socket.mdBindHost);
    c.socket.mdPort     = t["socket"]["md_port"].value_or(c.socket.mdPort);
    c.socket.session    = t["socket"]["session"].value_or(c.socket.session);
    c.socket.username   = t["socket"]["username"].value_or(c.socket.username);

    c.transport.interface = t["transport"]["interface"].value_or(c.transport.interface);
    c.transport.driver    = t["transport"]["driver"].value_or(c.transport.driver);
    c.transport.cpuCore   = t["transport"]["cpu_core"].value_or(c.transport.cpuCore);

    const net::MacAddr  lmac = config::parseMac(t["network"]["local_mac"].value_or(std::string{}));
    const net::MacAddr  pmac = config::parseMac(t["network"]["peer_mac"].value_or(std::string{}));
    const std::uint32_t lip  = config::parseIp(t["network"]["local_ip"].value_or(std::string{"0.0.0.0"}));
    const std::uint32_t pip  = config::parseIp(t["network"]["peer_ip"].value_or(std::string{"0.0.0.0"}));

    for (net::Endpoints* ep : {&c.transport.marketData, &c.transport.orderEntry}) {
        ep->srcMac = lmac;
        ep->dstMac = pmac;
        ep->srcIp  = lip;
        ep->dstIp  = pip;
    }
    c.transport.marketData.srcPort = t["market_data"]["src_port"].value_or(c.transport.marketData.srcPort);
    c.transport.marketData.dstPort = t["market_data"]["dst_port"].value_or(c.transport.marketData.dstPort);
    c.transport.orderEntry.srcPort = t["order_entry"]["src_port"].value_or(c.transport.orderEntry.srcPort);
    c.transport.orderEntry.dstPort = t["order_entry"]["dst_port"].value_or(c.transport.orderEntry.dstPort);

    return c;
}

}   // namespace abt::dut
