#pragma once
//
// Deterministic synthetic order flow that keeps a live two-sided market on the feed.
//

#include <cstddef>
#include <cstdint>
#include <vector>

#include "abt/lob/Types.hpp"

namespace abt::sim {

template <class Market>
class FlowGenerator {
public:
    struct Config {
        lob::Price     midTick    = 5200;
        lob::Price     halfSpread = 1;
        lob::Price     depthTicks = 20;
        lob::Quantity  minQty     = 10;
        lob::Quantity  maxQty     = 500;
        std::uint32_t  cancelPct  = 30;
        std::uint32_t  crossPct   = 15;
        std::uint64_t  seed       = 0x9E3779B97F4A7C15ull;
    };

    FlowGenerator(Market& market, const Config& cfg)
        : m_market(market), m_cfg(cfg), m_state(cfg.seed ? cfg.seed : 1) {}

    void step(std::uint64_t ts) {
        const std::uint32_t roll = static_cast<std::uint32_t>(rng() % 100);

        if (!m_live.empty() && roll < m_cfg.cancelPct) {
            const std::size_t i = static_cast<std::size_t>(rng() % m_live.size());
            m_market.cancelSynthetic(m_live[i], ts);
            m_live[i] = m_live.back();
            m_live.pop_back();
            return;
        }

        const bool buy = (rng() & 1u) != 0;
        const lob::Side side = buy ? lob::Side::Buy : lob::Side::Sell;
        const lob::Quantity qty = m_cfg.minQty +
            static_cast<lob::Quantity>(rng() % (m_cfg.maxQty - m_cfg.minQty + 1));
        const bool crossing = roll >= 100 - m_cfg.crossPct;

        lob::Price tick;
        if (crossing) {
            if (buy) tick = m_market.bestAsk() != lob::kNoPrice ? m_market.bestAsk() : m_cfg.midTick;
            else     tick = m_market.bestBid() != lob::kNoPrice ? m_market.bestBid() : m_cfg.midTick;
        } else {
            const lob::Price off = static_cast<lob::Price>(rng() % static_cast<std::uint64_t>(m_cfg.depthTicks));
            tick = buy ? m_cfg.midTick - m_cfg.halfSpread - off
                       : m_cfg.midTick + m_cfg.halfSpread + off;
        }

        const lob::OrderId ref = m_market.injectSynthetic(side, tick, qty, ts);
        if (!crossing) {
            m_live.push_back(ref);
            if (m_live.size() > kMaxTracked) {
                m_live.erase(m_live.begin(),
                             m_live.begin() + static_cast<std::ptrdiff_t>(kMaxTracked / 2));
            }
        }
    }

    void run(std::size_t steps, std::uint64_t tsStart, std::uint64_t tsStep) {
        std::uint64_t ts = tsStart;
        for (std::size_t i = 0; i < steps; ++i, ts += tsStep) step(ts);
    }

    [[nodiscard]] std::size_t liveCount() const noexcept { return m_live.size(); }

private:
    static constexpr std::size_t kMaxTracked = 4096;

    std::uint64_t rng() noexcept {
        std::uint64_t x = m_state;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        m_state = x;
        return x;
    }

    Market&                    m_market;
    Config                     m_cfg;
    std::uint64_t              m_state;
    std::vector<lob::OrderId>  m_live;
};

}
