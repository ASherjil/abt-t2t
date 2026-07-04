#pragma once
//
// Deterministic synthetic order flow that keeps a live two-sided market on the feed.
//

#include <cstddef>
#include <cstdint>
#include <vector>

#include "abt/lob/Types.hpp"

namespace abt {

template <class Market>
class FlowGenerator {
public:
    struct Config {
        Price          midTick    = 5200;
        Price          halfSpread = 1;
        Price          depthTicks = 20;
        Quantity       minQty     = 10;
        Quantity       maxQty     = 500;
        std::uint32_t  cancelPct  = 30;
        std::uint32_t  crossPct   = 15;
        std::uint64_t  seed       = 0x9E3779B97F4A7C15ull;
    };

    FlowGenerator(Market& market, const Config& cfg);

    void step(std::uint64_t ts);
    void run(std::size_t steps, std::uint64_t tsStart, std::uint64_t tsStep);
    [[nodiscard]] std::size_t liveCount() const noexcept { return m_live.size(); }

private:
    static constexpr std::size_t kMaxTracked = 4096;

    std::uint64_t rng() noexcept;

    Market&               m_market;
    Config                m_cfg;
    std::uint64_t         m_state;
    std::vector<OrderId>  m_live;
};

template <class Market>
FlowGenerator<Market>::FlowGenerator(Market& market, const Config& cfg)
    : m_market(market), m_cfg(cfg), m_state(cfg.seed ? cfg.seed : 1) {}

template <class Market>
void FlowGenerator<Market>::step(std::uint64_t ts) {
    const std::uint32_t roll = static_cast<std::uint32_t>(rng() % 100);

    if (!m_live.empty() && roll < m_cfg.cancelPct) {
        const std::size_t i = static_cast<std::size_t>(rng() % m_live.size());
        m_market.cancelSynthetic(m_live[i], ts);
        m_live[i] = m_live.back();
        m_live.pop_back();
        return;
    }

    const bool buy = (rng() & 1u) != 0;
    const Side side = buy ? Side::Buy : Side::Sell;
    const Quantity qty = m_cfg.minQty +
        static_cast<Quantity>(rng() % (m_cfg.maxQty - m_cfg.minQty + 1));
    const bool crossing = roll >= 100 - m_cfg.crossPct;

    Price tick;
    if (crossing) {
        if (buy) tick = m_market.bestAsk() != kNoPrice ? m_market.bestAsk() : m_cfg.midTick;
        else     tick = m_market.bestBid() != kNoPrice ? m_market.bestBid() : m_cfg.midTick;
    } else {
        const Price off = static_cast<Price>(rng() % static_cast<std::uint64_t>(m_cfg.depthTicks));
        tick = buy ? m_cfg.midTick - m_cfg.halfSpread - off
                   : m_cfg.midTick + m_cfg.halfSpread + off;
    }

    const OrderId ref = m_market.injectSynthetic(side, tick, qty, ts);
    if (!crossing) {
        m_live.push_back(ref);
        if (m_live.size() > kMaxTracked) {
            m_live.erase(m_live.begin(),
                         m_live.begin() + static_cast<std::ptrdiff_t>(kMaxTracked / 2));
        }
    }
}

template <class Market>
void FlowGenerator<Market>::run(std::size_t steps, std::uint64_t tsStart, std::uint64_t tsStep) {
    std::uint64_t ts = tsStart;
    for (std::size_t i = 0; i < steps; ++i, ts += tsStep) step(ts);
}

template <class Market>
std::uint64_t FlowGenerator<Market>::rng() noexcept {
    std::uint64_t x = m_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    m_state = x;
    return x;
}

}
