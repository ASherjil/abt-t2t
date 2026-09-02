#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "t2t/replay/ItchFile.hpp"
#include "t2t/replay/SymbolFilter.hpp"
#include "t2t/util/Clock.hpp"

namespace abt {

struct ReplayConfig {
    bool          enabled = false;
    std::string   file;
    double        speed        = 1.0;
    std::uint32_t loops        = 0;
    std::uint64_t skipToNs     = 0;
    std::uint64_t stopAtNs     = 0;
    std::size_t   preloadMaxMb = 1024;
    std::size_t   maxBatch     = 64;
    bool          waitForDut   = true;
};

struct ReplayProgress {
    std::uint32_t loop        = 0;
    std::uint64_t sent        = 0;
    std::uint64_t virtualTs   = 0;
    std::uint64_t maxLateNs   = 0;
    std::uint64_t lateOver1ms = 0;
    bool          preloaded   = false;
    bool          mapped      = false;
    bool          finished    = false;
};

template <class Session>
class MarketReplay {
public:
    MarketReplay(Session& session, const ReplayConfig& cfg);

    [[nodiscard]] bool                  open();
    [[nodiscard]] bool                  pump(std::uint64_t nowMono);
    [[nodiscard]] const ReplayProgress& progress() const noexcept;
    [[nodiscard]] std::uint64_t         fileMessages() const noexcept;

private:
    [[nodiscard]] bool fetch();
    [[nodiscard]] bool restart();
    void               endOfLoop();

    Session*                              m_session;
    ReplayConfig                          m_cfg;
    ReplayProgress                        m_progress{};
    std::vector<std::byte>                m_store;
    std::size_t                           m_cursor = 0;
    std::optional<replay::ItchFileReader> m_reader;
    std::uint64_t                         m_fileMessages = 0;
    std::span<const std::byte>            m_cur;
    bool                                  m_have       = false;
    bool                                  m_anchored   = false;
    std::uint64_t                         m_anchorMono = 0;
    std::uint64_t                         m_anchorTs   = 0;
    double                                m_nsPerNs    = 1.0;
};

template <class Session>
MarketReplay<Session>::MarketReplay(Session& session, const ReplayConfig& cfg)
    : m_session(&session),
      m_cfg(cfg),
      m_nsPerNs(cfg.speed > 0.0 ? 1.0 / cfg.speed : 0.0) {
}

template <class Session>
bool MarketReplay<Session>::open() {
    replay::ItchFileReader probe(m_cfg.file);
    if (!probe.ok()) {
        return false;
    }
    std::span<const std::byte> msg;
    if (probe.mapped()) {
        while (probe.next(msg)) {
            ++m_fileMessages;
        }
        m_progress.mapped = true;
        m_reader.emplace(m_cfg.file);
        return m_reader->ok() && m_fileMessages > 0;
    }
    const std::size_t cap = m_cfg.preloadMaxMb << 20;
    while (probe.next(msg)) {
        if (m_store.size() + msg.size() + 2 > cap) {
            m_store.clear();
            m_store.shrink_to_fit();
            m_fileMessages = 0;
            return restart();
        }
        m_store.push_back(static_cast<std::byte>(msg.size() >> 8));
        m_store.push_back(static_cast<std::byte>(msg.size() & 0xff));
        m_store.insert(m_store.end(), msg.begin(), msg.end());
        ++m_fileMessages;
    }
    m_progress.preloaded = true;
    return m_fileMessages > 0;
}

template <class Session>
bool MarketReplay<Session>::restart() {
    m_cursor   = 0;
    m_have     = false;
    m_anchored = false;
    if (m_progress.preloaded) {
        return true;
    }
    if (m_reader.has_value()) {
        m_reader->reset();
        return true;
    }
    m_reader.emplace(m_cfg.file);
    return m_reader->ok();
}

template <class Session>
bool MarketReplay<Session>::fetch() {
    if (m_progress.preloaded) {
        if (m_cursor + 2 > m_store.size()) {
            return false;
        }
        const std::size_t len = (std::to_integer<std::size_t>(m_store[m_cursor]) << 8) |
                                std::to_integer<std::size_t>(m_store[m_cursor + 1]);
        m_cursor += 2;
        m_cur = {m_store.data() + m_cursor, len};
        m_cursor += len;
        return true;
    }
    if (!m_reader.has_value() && !restart()) {
        return false;
    }
    if (!m_reader->next(m_cur)) {
        return false;
    }
    if (!m_progress.mapped) {
        ++m_fileMessages;
    }
    return true;
}

template <class Session>
void MarketReplay<Session>::endOfLoop() {
    m_session->resetDay(m_progress.virtualTs);
    m_session->flushMarketData();
    ++m_progress.loop;
    if (m_cfg.loops != 0 && m_progress.loop >= m_cfg.loops) {
        m_progress.finished = true;
        return;
    }
    if (!restart()) {
        m_progress.finished = true;
    }
}

template <class Session>
bool MarketReplay<Session>::pump(std::uint64_t nowMono) {
    if (m_progress.finished) {
        return false;
    }
    std::size_t batch = 0;
    for (;;) {
        if (!m_have) {
            if (!fetch()) {
                endOfLoop();
                if (m_progress.finished) {
                    return false;
                }
                continue;
            }
            m_have = true;
        }
        if (m_cur.size() < 11) {
            m_have = false;
            continue;
        }
        const std::uint64_t ts = replay::SymbolFilter::timestampOf(m_cur);
        if (m_cfg.stopAtNs != 0 && ts > m_cfg.stopAtNs) {
            m_have = false;
            endOfLoop();
            if (m_progress.finished) {
                return false;
            }
            continue;
        }
        if (!m_anchored && m_nsPerNs > 0.0 && (m_cfg.skipToNs == 0 || ts >= m_cfg.skipToNs)) {
            m_anchored   = true;
            m_anchorMono = nowMono;
            m_anchorTs   = ts;
        }
        if (m_anchored) {
            const std::uint64_t due = ts > m_anchorTs
                                          ? m_anchorMono +
                                                static_cast<std::uint64_t>(
                                                    static_cast<double>(ts - m_anchorTs) * m_nsPerNs)
                                          : m_anchorMono;
            if (due > nowMono) {
                nowMono = monotonicNs();
                if (due > nowMono) {
                    m_session->flushMarketData();
                    return true;
                }
            }
            const std::uint64_t late = nowMono - due;
            m_progress.maxLateNs     = std::max(late, m_progress.maxLateNs);
            if (late > 1'000'000ull) {
                ++m_progress.lateOver1ms;
            }
        }
        m_session->replayMessage(m_cur, ts);
        m_progress.virtualTs = ts;
        ++m_progress.sent;
        m_have = false;
        if (++batch >= m_cfg.maxBatch) {
            m_session->flushMarketData();
            return true;
        }
    }
}

template <class Session>
const ReplayProgress& MarketReplay<Session>::progress() const noexcept {
    return m_progress;
}

template <class Session>
std::uint64_t MarketReplay<Session>::fileMessages() const noexcept {
    return m_fileMessages;
}

}   // namespace abt
