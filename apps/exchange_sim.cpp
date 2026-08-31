#include <csignal>

#include <fmt/core.h>

#include "SimBackend.hpp"

#include "abt/sim/SimConfig.hpp"
#include "abt/sim/SimRunner.hpp"

using namespace abt;

static_assert(SimBackendTraits<SimBackend>);

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

void installSignals() {
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

}

int main() {
    const SimConfig cfg = loadConfig(ABT_CONFIG_PATH);
    installSignals();

    if (cfg.transport.mode != SimBackend::kName) {
        fmt::print(stderr, "exchange-sim: built for backend '{}' but config transport.mode is '{}'\n",
                   SimBackend::kName, cfg.transport.mode);
        return 1;
    }

    auto backend = SimBackend::make(cfg);
    if (!SimBackend::init(backend, cfg)) {
        fmt::print(stderr, "exchange-sim: backend '{}' init failed\n", SimBackend::kName);
        return 1;
    }

    const int rc = runSim<SimBackend>(cfg, backend, g_stop);
    fmt::print(stderr, "exchange-sim: shut down.\n");
    return rc;
}
