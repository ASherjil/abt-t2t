#include <csignal>

#include <fmt/core.h>

#include "Backend.hpp"

#include "abt/dut/DutAppConfig.hpp"
#include "abt/dut/DutRunner.hpp"
#include "abt/util/Tsc.hpp"

using namespace abt;

static_assert(BackendTraits<Backend>);

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
    const dut::DutAppConfig cfg = dut::loadDutConfig(ABT_DUT_CONFIG_PATH);
    installSignals();
    tsc::warmUp();

    const NicSpec nic = dut::nicOf(cfg);
    auto backend = Backend::make(nic);
    if (!Backend::init(backend, nic)) {
        fmt::print(stderr, "dut: backend '{}' init failed\n", Backend::kName);
        return 1;
    }

    const int rc = dut::runDut<Backend>(cfg, backend, g_stop);
    fmt::print(stderr, "dut: shut down.\n");
    return rc;
}
