#include "Backend.hpp"

#include <csignal>
#include <cstring>

#include <fmt/core.h>

#include "t2t/BuildConfig.hpp"
#include "t2t/dut/DutAppConfig.hpp"
#include "t2t/dut/DutRunner.hpp"
#include "t2t/util/MemLock.hpp"
#include "t2t/util/Tsc.hpp"

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
    const dut::DutAppConfig cfg = dut::loadDutConfig(ABT_DUT_CONFIG_PATH);
    installSignals();
    if (const auto lock = util::lockAndPrefaultMemory(); !lock.locked) {
        fmt::print(stderr, "dut: mlockall failed ({}), page faults may hit the hot loop\n",
                   std::strerror(lock.error));
    }
    if constexpr (build::kSwTiming) {
        tsc::warmUp();
    }

    const NicSpec nic     = dut::nicOf(cfg);
    auto          backend = Backend::make(nic);
    if (!Backend::init(backend, nic)) {
        fmt::print(stderr, "dut: backend '{}' init failed\n", Backend::kName);
        return 1;
    }

    const int rc = dut::runDut<Backend>(cfg, backend, g_stop);
    fmt::print(stderr, "dut: shut down.\n");
    return rc;
}
