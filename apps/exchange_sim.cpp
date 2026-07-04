//
// Runnable exchange simulator (config 1): TCP order entry + UDP market data via kernel sockets.
//

#include <csignal>
#include <cstdint>
#include <cstdlib>

#include <fmt/core.h>

#include "abt/protocol/Itch50.hpp"
#include "abt/sim/ExchangeSession.hpp"
#include "abt/sim/FlowGenerator.hpp"
#include "abt/util/Clock.hpp"

using namespace abt;

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

int main(int argc, char** argv) {
    const std::uint16_t oePort = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 5001;
    const char* mdHost         = argc > 2 ? argv[2] : "127.0.0.1";
    const std::uint16_t mdPort = argc > 3 ? static_cast<std::uint16_t>(std::atoi(argv[3])) : 5002;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    using Session = ExchangeSession<IoMode::Socket>;
    Session ex{};
    ex.prepareSocketIo(oePort, mdHost, mdPort);
    fmt::print(stderr, "exchange-sim: publishing market data to udp/{}:{}\n", mdHost, mdPort);

    FlowGenerator<Session> gen(ex, {});
    ex.sessionEvent(itch::SystemEventCode::StartOfMarketHours, nsSinceMidnightUtc());
    gen.run(200, nsSinceMidnightUtc(), 0);

    ex.run(g_stop, [&](std::uint64_t ts) { gen.step(ts); });

    ex.sessionEvent(itch::SystemEventCode::EndOfMarketHours, nsSinceMidnightUtc());
    fmt::print(stderr, "exchange-sim: shut down.\n");
    return 0;
}
