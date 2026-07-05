//
// TOML (toml++) loader for the exchange simulator's runtime configuration.
//

#include "abt/sim/SimConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <toml++/toml.hpp>

namespace abt {

namespace {

[[nodiscard]] int hexVal(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] net::MacAddr parseMac(std::string_view s) noexcept {
    net::MacAddr mac{};
    std::size_t idx = 0;
    unsigned acc = 0;
    int digits = 0;
    const auto flush = [&] {
        if (digits > 0 && idx < mac.size()) mac[idx++] = static_cast<std::uint8_t>(acc);
        acc = 0;
        digits = 0;
    };
    for (char c : s) {
        if (c == ':' || c == '-') { flush(); continue; }
        const int h = hexVal(c);
        if (h < 0) continue;
        acc = acc * 16u + static_cast<unsigned>(h);
        ++digits;
    }
    flush();
    return mac;
}

[[nodiscard]] std::uint32_t parseIp(std::string_view s) noexcept {
    std::array<std::uint8_t, 4> oct{};
    std::size_t idx = 0;
    unsigned acc = 0;
    const auto flush = [&] {
        if (idx < oct.size()) oct[idx++] = static_cast<std::uint8_t>(acc);
        acc = 0;
    };
    for (char c : s) {
        if (c == '.') { flush(); continue; }
        if (c >= '0' && c <= '9') acc = acc * 10u + static_cast<unsigned>(c - '0');
    }
    flush();
    return net::ipv4(oct[0], oct[1], oct[2], oct[3]);
}

}  // namespace

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

    c.flow.midTick    = t["flow"]["mid_tick"].value_or(c.flow.midTick);
    c.flow.halfSpread = t["flow"]["half_spread"].value_or(c.flow.halfSpread);
    c.flow.depthTicks = t["flow"]["depth_ticks"].value_or(c.flow.depthTicks);
    c.flow.minQty     = t["flow"]["min_qty"].value_or(c.flow.minQty);
    c.flow.maxQty     = t["flow"]["max_qty"].value_or(c.flow.maxQty);
    c.flow.cancelPct  = t["flow"]["cancel_pct"].value_or(c.flow.cancelPct);
    c.flow.crossPct   = t["flow"]["cross_pct"].value_or(c.flow.crossPct);
    c.flow.seed       = t["flow"]["seed"].value_or(c.flow.seed);
    c.warmupSteps     = t["flow"]["warmup_steps"].value_or(c.warmupSteps);
    c.tickIntervalNs  = t["flow"]["tick_interval_ns"].value_or(c.tickIntervalNs);

    c.dpdk.interface = t["transport"]["interface"].value_or(c.dpdk.interface);
    c.dpdk.driver    = t["transport"]["driver"].value_or(c.dpdk.driver);
    c.dpdk.cpuCore   = t["transport"]["cpu_core"].value_or(c.dpdk.cpuCore);

    const net::MacAddr lmac = parseMac(t["network"]["local_mac"].value_or(std::string{}));
    const net::MacAddr pmac = parseMac(t["network"]["peer_mac"].value_or(std::string{}));
    const std::uint32_t lip = parseIp(t["network"]["local_ip"].value_or(std::string{"0.0.0.0"}));
    const std::uint32_t pip = parseIp(t["network"]["peer_ip"].value_or(std::string{"0.0.0.0"}));

    for (net::Endpoints* ep : {&c.dpdk.marketData, &c.dpdk.orderEntry}) {
        ep->srcMac = lmac;
        ep->dstMac = pmac;
        ep->srcIp  = lip;
        ep->dstIp  = pip;
    }
    c.dpdk.marketData.srcPort = t["market_data"]["src_port"].value_or(c.dpdk.marketData.srcPort);
    c.dpdk.marketData.dstPort = t["market_data"]["dst_port"].value_or(c.dpdk.marketData.dstPort);
    c.dpdk.orderEntry.srcPort = t["order_entry"]["src_port"].value_or(c.dpdk.orderEntry.srcPort);
    c.dpdk.orderEntry.dstPort = t["order_entry"]["dst_port"].value_or(c.dpdk.orderEntry.dstPort);

    c.socket.oePort = t["socket"]["oe_port"].value_or(c.socket.oePort);
    c.socket.mdHost = t["socket"]["md_host"].value_or(c.socket.mdHost);
    c.socket.mdPort = t["socket"]["md_port"].value_or(c.socket.mdPort);

    return c;
}

}  // namespace abt
