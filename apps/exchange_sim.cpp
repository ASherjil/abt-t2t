#include "Backend.hpp"

#include <csignal>

#include <fmt/core.h>

#include "abt/sim/SimConfig.hpp"
#include "abt/sim/SimRunner.hpp"

using namespace abt;

static_assert(BackendTraits<Backend>);

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

void installSignals() {
    struct sigaction sa {};

    sa.sa_handler = onSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

}   // namespace

int main() {
    const SimConfig cfg = loadConfig(ABT_CONFIG_PATH);
    installSignals();

    const NicSpec nic     = nicOf(cfg);
    auto          backend = Backend::make(nic);
    if (!Backend::init(backend, nic)) {
        fmt::print(stderr, "exchange-sim: backend '{}' init failed\n", Backend::kName);
        return 1;
    }

    const int rc = runSim<Backend>(cfg, backend, g_stop);
    fmt::print(stderr, "exchange-sim: shut down.\n");
    return rc;
}
