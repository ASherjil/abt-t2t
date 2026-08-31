#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "TestHarness.hpp"

#include "abt/dut/DutAppConfig.hpp"

using namespace abt;

namespace {

const char* kToml = R"(
[venue]
symbol = "MSFT"
min_price = 100
max_price = 900000
tick_wire = 50
first_user_ref = 77
max_orders = 8192

[quoter]
half_spread_ticks = 3
quote_qty = 250
skew_ticks_per_unit = 0.002

[measure]
histogram_core = 7
queue_capacity = 1024
sig_figs = 4

[socket]
oe_host = "10.1.1.1"
oe_port = 6001
md_bind_host = "10.1.1.2"
md_port = 6002
session = "TST0000042"
username = "USR42"

[transport]
mode = "efvi"
interface = "sfc1"
cpu_core = 6

[network]
local_mac = "aa:bb:cc:dd:ee:02"
local_ip  = "10.0.0.2"
peer_mac  = "11:22:33:44:55:66"
peer_ip   = "10.0.0.1"

[market_data]
src_port = 41000
dst_port = 40000

[order_entry]
src_port = 41001
dst_port = 40001
)";

std::string writeTemp() {
    const std::string path = "dutconfig_test_tmp.toml";
    std::ofstream f(path);
    f << kToml;
    return path;
}

int octet(std::uint32_t ip, int shift) {
    return static_cast<int>((ip >> shift) & 0xFFu);
}

void test_load() {
    const std::string path = writeTemp();
    const dut::DutAppConfig c = dut::loadDutConfig(path);
    std::remove(path.c_str());

    CHECK(c.session.symbol == "MSFT");
    CHECK_EQ(c.session.minPrice, 100);
    CHECK_EQ(c.session.maxPrice, 900000);
    CHECK_EQ(c.session.tickWire, 50);
    CHECK_EQ(c.session.firstUserRef, 77u);
    CHECK_EQ(c.session.maxOrders, 8192u);
    CHECK_EQ(c.session.queueCapacity, 1024u);
    CHECK_EQ(c.session.sigFigs, 4);

    CHECK_EQ(c.quoter.tickWire, 50);
    CHECK_EQ(c.quoter.minPrice, 100);
    CHECK_EQ(c.quoter.maxPrice, 900000);
    CHECK_EQ(c.quoter.halfSpreadTicks, 3);
    CHECK_EQ(c.quoter.quoteQty, 250u);
    CHECK(c.quoter.skewTicksPerUnit > 0.0019 && c.quoter.skewTicksPerUnit < 0.0021);

    CHECK_EQ(c.measure.histogramCore, 7);

    CHECK(c.socket.oeHost == "10.1.1.1");
    CHECK_EQ(c.socket.oePort, 6001u);
    CHECK(c.socket.mdBindHost == "10.1.1.2");
    CHECK_EQ(c.socket.mdPort, 6002u);
    CHECK(c.socket.session == "TST0000042");
    CHECK(c.socket.username == "USR42");

    CHECK(c.transport.mode == "efvi");
    CHECK(c.transport.interface == "sfc1");
    CHECK_EQ(c.transport.cpuCore, 6);
    CHECK_EQ(c.transport.orderEntry.srcMac[5], 0x02u);
    CHECK_EQ(c.transport.orderEntry.dstMac[0], 0x11u);
    CHECK_EQ(octet(c.transport.marketData.srcIp, 0), 2);
    CHECK_EQ(octet(c.transport.marketData.dstIp, 0), 1);
    CHECK_EQ(c.transport.marketData.srcPort, 41000u);
    CHECK_EQ(c.transport.marketData.dstPort, 40000u);
    CHECK_EQ(c.transport.orderEntry.srcPort, 41001u);
    CHECK_EQ(c.transport.orderEntry.dstPort, 40001u);
}

void test_defaults() {
    const std::string path = "dutconfig_test_empty.toml";
    {
        std::ofstream f(path);
        f << "[venue]\n";
    }
    const dut::DutAppConfig c = dut::loadDutConfig(path);
    std::remove(path.c_str());
    CHECK_EQ(c.session.tickWire, 1);
    CHECK_EQ(c.session.firstUserRef, 1u);
    CHECK(c.transport.mode == "socket");
    CHECK_EQ(c.measure.histogramCore, -1);
}

}

int main() {
    test_load();
    test_defaults();
    return abt::test::summary("dutconfig");
}
