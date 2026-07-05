//
// Runnable exchange simulator (config 1): all settings from config/exchange_sim.toml, no CLI args.
//

#include <csignal>
#include <cstdint>

#include <fmt/core.h>

#include "abt/protocol/Itch50.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "abt/sim/FlowGenerator.hpp"
#include "abt/sim/SimConfig.hpp"
#include "abt/util/Clock.hpp"

using namespace abt;

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

int main() {
    const SimConfig cfg = loadConfig(ABT_CONFIG_PATH);

    struct sigaction sa{};
    sa.sa_handler = onSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    using Session = ExchangeSession<IoMode::Socket>;
    Session ex{cfg.venue};
    if (!ex.prepareSocketIo(cfg.socket.oePort, cfg.socket.mdHost.c_str(), cfg.socket.mdPort)) {
        fmt::print(stderr, "exchange-sim: interrupted before a client connected.\n");
        return 0;
    }
    fmt::print(stderr, "exchange-sim: publishing market data to udp/{}:{}\n",
               cfg.socket.mdHost, cfg.socket.mdPort);

    FlowGenerator<Session> gen(ex, cfg.flow);
    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, nsSinceMidnightUtc());
    gen.run(cfg.warmupSteps, nsSinceMidnightUtc(), 0);

    ex.run(g_stop, cfg.tickIntervalNs, [&](std::uint64_t ts) { gen.step(ts); });

    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, nsSinceMidnightUtc());
    fmt::print(stderr, "exchange-sim: shut down.\n");
    return 0;
}
