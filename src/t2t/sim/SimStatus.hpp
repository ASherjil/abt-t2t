#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <thread>

#include <rigtorp/SPSCQueue.h>

#include "t2t/lob/Types.hpp"
#include "t2t/sim/ExchangeSession.hpp"
#include "t2t/sim/MarketReplay.hpp"
#include "t2t/sim/Venue.hpp"

namespace abt {

inline constexpr std::size_t kSimStatusQueue = 64;
inline constexpr auto        kSimStatusPoll  = std::chrono::milliseconds(20);

struct SimStatus {
    std::uint64_t  elapsedNs = 0;
    ReplayProgress progress{};
    SessionStats   stats{};
    MirrorStats    mirror{};
    Price          bid     = kNoPrice;
    Price          ask     = kNoPrice;
    std::size_t    live    = 0;
    std::size_t    clients = 0;
};

template <class Session>
[[nodiscard]] SimStatus simStatus(const Session& ex, const ReplayProgress& p, std::uint64_t elapsedNs) {
    return SimStatus{.elapsedNs = elapsedNs,
                     .progress  = p,
                     .stats     = ex.stats(),
                     .mirror    = ex.mirrorStats(),
                     .bid       = ex.bestBid(),
                     .ask       = ex.bestAsk(),
                     .live      = ex.liveOrders(),
                     .clients   = ex.clientOrders()};
}

void printSimStatus(const SimStatus& st);

class SimStatusThread {
public:
    SimStatusThread(int core, int hotCore);

    bool push(const SimStatus& st) noexcept;
    void stop();

private:
    using Queue = rigtorp::SPSCQueue<SimStatus>;

    static void place(int core, int hotCore) noexcept;
    static void drain(Queue& q);
    static void loop(const std::stop_token& stop, Queue* q, int core, int hotCore);

    Queue        m_queue{kSimStatusQueue};
    std::jthread m_thread;
};

}   // namespace abt
