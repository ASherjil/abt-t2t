//
// Unit test for the TOML config loader (abt::loadConfig): field parsing + MAC/IP extraction.
//

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "TestHarness.hpp"

#include "abt/sim/SimConfig.hpp"

using namespace abt;

namespace {

const char* kToml = R"(
[venue]
symbol = "MSFT"
stock_locate = 7
session = "TST0000042"
min_tick = 5
max_tick = 90000
wire_per_tick = 50

[flow]
mid_tick = 4200
cancel_pct = 40
seed = 123456789
warmup_steps = 111
tick_interval_ns = 250000

[transport]
interface = "cx1"
driver = "af_xdp"
cpu_core = 5

[network]
local_mac = "aa:bb:cc:dd:ee:01"
local_ip  = "10.0.0.1"
peer_mac  = "11:22:33:44:55:66"
peer_ip   = "192.168.1.2"

[market_data]
src_port = 40000
dst_port = 41000

[order_entry]
src_port = 40001
dst_port = 41001

[socket]
oe_port = 6001
md_host = "127.0.0.9"
md_port = 6002
)";

std::string writeTemp() {
    const std::string path = "config_test_tmp.toml";
    std::ofstream f(path);
    f << kToml;
    return path;
}

int octet(std::uint32_t ip, int shift) { return static_cast<int>((ip >> shift) & 0xFFu); }

void test_load() {
    const std::string path = writeTemp();
    const SimConfig c = loadConfig(path);
    std::remove(path.c_str());

    CHECK(c.venue.symbol == "MSFT");
    CHECK_EQ(c.venue.stockLocate, 7u);
    CHECK(c.venue.session == "TST0000042");
    CHECK_EQ(c.venue.minTick, 5);
    CHECK_EQ(c.venue.maxTick, 90000);
    CHECK_EQ(c.venue.wirePerTick, 50u);

    CHECK_EQ(c.flow.midTick, 4200);
    CHECK_EQ(c.flow.cancelPct, 40u);
    CHECK_EQ(c.flow.seed, 123456789u);
    CHECK_EQ(c.warmupSteps, 111u);
    CHECK_EQ(c.tickIntervalNs, 250000u);

    CHECK(c.dpdk.interface == "cx1");
    CHECK(c.dpdk.driver == "af_xdp");
    CHECK_EQ(c.dpdk.cpuCore, 5);

    CHECK_EQ(c.dpdk.marketData.srcMac[0], 0xaau);
    CHECK_EQ(c.dpdk.marketData.srcMac[5], 0x01u);
    CHECK_EQ(c.dpdk.orderEntry.dstMac[0], 0x11u);
    CHECK_EQ(c.dpdk.orderEntry.dstMac[5], 0x66u);
    CHECK_EQ(octet(c.dpdk.marketData.srcIp, 24), 10);
    CHECK_EQ(octet(c.dpdk.marketData.srcIp, 0), 1);
    CHECK_EQ(octet(c.dpdk.orderEntry.dstIp, 24), 192);
    CHECK_EQ(octet(c.dpdk.orderEntry.dstIp, 0), 2);

    CHECK_EQ(c.dpdk.marketData.srcPort, 40000u);
    CHECK_EQ(c.dpdk.marketData.dstPort, 41000u);
    CHECK_EQ(c.dpdk.orderEntry.srcPort, 40001u);
    CHECK_EQ(c.dpdk.orderEntry.dstPort, 41001u);

    CHECK_EQ(c.socket.oePort, 6001u);
    CHECK(c.socket.mdHost == "127.0.0.9");
    CHECK_EQ(c.socket.mdPort, 6002u);
}

}

int main() {
    test_load();
    return abt::test::summary("config");
}
