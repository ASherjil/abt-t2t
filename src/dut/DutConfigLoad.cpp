#include "abt/dut/DutAppConfig.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include "abt/config/NetParse.hpp"

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

    c.session.symbol        = t["venue"]["symbol"].value_or(c.session.symbol);
    c.session.minPrice      = t["venue"]["min_price"].value_or(c.session.minPrice);
    c.session.maxPrice      = t["venue"]["max_price"].value_or(c.session.maxPrice);
    c.session.tickWire      = t["venue"]["tick_wire"].value_or(c.session.tickWire);
    c.session.firstUserRef  = t["venue"]["first_user_ref"].value_or(c.session.firstUserRef);
    c.session.maxOrders     = t["venue"]["max_orders"].value_or(c.session.maxOrders);

    c.quoter.tickWire         = c.session.tickWire;
    c.quoter.minPrice         = c.session.minPrice;
    c.quoter.maxPrice         = c.session.maxPrice;
    c.quoter.halfSpreadTicks  = t["quoter"]["half_spread_ticks"].value_or(c.quoter.halfSpreadTicks);
    c.quoter.quoteQty         = t["quoter"]["quote_qty"].value_or(c.quoter.quoteQty);
    c.quoter.skewTicksPerUnit = t["quoter"]["skew_ticks_per_unit"].value_or(c.quoter.skewTicksPerUnit);

    c.measure.histogramCore = t["measure"]["histogram_core"].value_or(c.measure.histogramCore);
    c.measure.queueCapacity = t["measure"]["queue_capacity"].value_or(c.measure.queueCapacity);
    c.measure.sigFigs       = t["measure"]["sig_figs"].value_or(c.measure.sigFigs);
    c.session.queueCapacity = c.measure.queueCapacity;
    c.session.sigFigs       = c.measure.sigFigs;

    c.socket.oeHost     = t["socket"]["oe_host"].value_or(c.socket.oeHost);
    c.socket.oePort     = t["socket"]["oe_port"].value_or(c.socket.oePort);
    c.socket.mdBindHost = t["socket"]["md_bind_host"].value_or(c.socket.mdBindHost);
    c.socket.mdPort     = t["socket"]["md_port"].value_or(c.socket.mdPort);
    c.socket.session    = t["socket"]["session"].value_or(c.socket.session);
    c.socket.username   = t["socket"]["username"].value_or(c.socket.username);

    c.transport.mode      = t["transport"]["mode"].value_or(c.transport.mode);
    c.transport.interface = t["transport"]["interface"].value_or(c.transport.interface);
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
    c.transport.marketData.srcPort =
        t["market_data"]["src_port"].value_or(c.transport.marketData.srcPort);
    c.transport.marketData.dstPort =
        t["market_data"]["dst_port"].value_or(c.transport.marketData.dstPort);
    c.transport.orderEntry.srcPort =
        t["order_entry"]["src_port"].value_or(c.transport.orderEntry.srcPort);
    c.transport.orderEntry.dstPort =
        t["order_entry"]["dst_port"].value_or(c.transport.orderEntry.dstPort);

    return c;
}

}
