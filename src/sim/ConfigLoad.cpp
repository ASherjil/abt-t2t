#include "abt/sim/SimConfig.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include "abt/config/NetParse.hpp"
#include "abt/replay/SymbolFilter.hpp"

namespace abt {

SimConfig loadConfig(const std::string& path) {
    SimConfig c{};

    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        fmt::print(stderr, "config: cannot parse {}: {}\n", path, e.description());
        std::exit(1);
    }

    c.venue.symbol      = t["venue"]["symbol"].value_or(c.venue.symbol);
    c.venue.stockLocate = t["venue"]["stock_locate"].value_or(c.venue.stockLocate);
    c.venue.session     = t["venue"]["session"].value_or(c.venue.session);
    c.venue.minTick     = t["venue"]["min_tick"].value_or(c.venue.minTick);
    c.venue.maxTick     = t["venue"]["max_tick"].value_or(c.venue.maxTick);
    c.venue.wirePerTick = t["venue"]["wire_per_tick"].value_or(c.venue.wirePerTick);
    c.venue.mdMaxPayload = t["venue"]["md_max_payload"].value_or(c.venue.mdMaxPayload);
    c.venue.liveReserve  = t["venue"]["order_reserve"].value_or(c.venue.liveReserve);

    c.replay.enabled      = t["replay"]["enabled"].value_or(c.replay.enabled);
    c.replay.file         = t["replay"]["file"].value_or(c.replay.file);
    c.replay.speed        = t["replay"]["speed"].value_or(c.replay.speed);
    c.replay.loops        = t["replay"]["loops"].value_or(c.replay.loops);
    c.replay.preloadMaxMb = t["replay"]["preload_max_mb"].value_or(c.replay.preloadMaxMb);
    c.replay.maxBatch     = t["replay"]["max_batch"].value_or(c.replay.maxBatch);
    c.replay.waitForDut   = t["replay"]["wait_for_dut"].value_or(c.replay.waitForDut);
    c.replay.skipToNs     = replay::parseTimeOfDay(t["replay"]["skip_to"].value_or(std::string{}));
    c.replay.stopAtNs     = replay::parseTimeOfDay(t["replay"]["stop_at"].value_or(std::string{}));
    const std::int64_t defaultFirstRef = c.replay.enabled ? (std::int64_t{1} << 62) : 1;
    c.venue.firstOrderRef =
        static_cast<OrderId>(t["venue"]["first_order_ref"].value_or(defaultFirstRef));

    c.flow.midTick    = t["flow"]["mid_tick"].value_or(c.flow.midTick);
    c.flow.halfSpread = t["flow"]["half_spread"].value_or(c.flow.halfSpread);
    c.flow.depthTicks = t["flow"]["depth_ticks"].value_or(c.flow.depthTicks);
    c.flow.minQty     = t["flow"]["min_qty"].value_or(c.flow.minQty);
    c.flow.maxQty     = t["flow"]["max_qty"].value_or(c.flow.maxQty);
    c.flow.cancelPct  = t["flow"]["cancel_pct"].value_or(c.flow.cancelPct);
    c.flow.crossPct   = t["flow"]["cross_pct"].value_or(c.flow.crossPct);
    c.flow.maxLive    = t["flow"]["max_live"].value_or(c.flow.maxLive);
    c.flow.seed       = t["flow"]["seed"].value_or(c.flow.seed);
    c.warmupSteps     = t["flow"]["warmup_steps"].value_or(c.warmupSteps);
    c.tickIntervalNs  = t["flow"]["tick_interval_ns"].value_or(c.tickIntervalNs);

    c.transport.interface = t["transport"]["interface"].value_or(c.transport.interface);
    c.transport.driver    = t["transport"]["driver"].value_or(c.transport.driver);
    c.transport.cpuCore   = t["transport"]["cpu_core"].value_or(c.transport.cpuCore);

    const net::MacAddr lmac = config::parseMac(t["network"]["local_mac"].value_or(std::string{}));
    const net::MacAddr pmac = config::parseMac(t["network"]["peer_mac"].value_or(std::string{}));
    const std::uint32_t lip =
        config::parseIp(t["network"]["local_ip"].value_or(std::string{"0.0.0.0"}));
    const std::uint32_t pip =
        config::parseIp(t["network"]["peer_ip"].value_or(std::string{"0.0.0.0"}));

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

    c.socket.oePort = t["socket"]["oe_port"].value_or(c.socket.oePort);
    c.socket.mdHost = t["socket"]["md_host"].value_or(c.socket.mdHost);
    c.socket.mdPort = t["socket"]["md_port"].value_or(c.socket.mdPort);

    return c;
}

}
