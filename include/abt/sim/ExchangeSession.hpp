#pragma once
//
// Session server: SoupBinTCP <-> OUCH <-> matching <-> ITCH <-> MoldUDP64 over an abstract Io.
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "abt/sim/Venue.hpp"

namespace abt::sim {

template <class Io>
class ExchangeSession {
public:
    struct Config {
        std::string   symbol      = "AAPL";
        std::uint16_t stockLocate = 1;
        std::string   session     = "SIM0000001";
        lob::Price    minTick     = 1;
        lob::Price    maxTick     = 100000;
        std::uint32_t wirePerTick = 100;
    };

    ExchangeSession(Io& io, const Config& cfg)
        : m_io(io),
          m_cfg(cfg),
          m_sink{this},
          m_packer(cfg.session, 1),
          m_venue(m_sink, cfg.symbol, cfg.stockLocate, cfg.minTick, cfg.maxTick,
                  cfg.wirePerTick) {}

    void onOrderEntryBytes(std::span<const std::byte> data, std::uint64_t ts) {
        m_rxBuf.insert(m_rxBuf.end(), data.begin(), data.end());
        std::size_t off = 0;
        soup::Packet p{};
        for (;;) {
            const std::size_t c =
                soup::parse({m_rxBuf.data() + off, m_rxBuf.size() - off}, p);
            if (c == 0) break;
            handleSoup(p, ts);
            off += c;
        }
        if (off) m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + static_cast<std::ptrdiff_t>(off));
    }

    lob::OrderId injectSynthetic(lob::Side side, lob::Price tick, lob::Quantity qty,
                                 std::uint64_t ts) {
        lob::OrderId ref = 0;
        withMarketData([&] { ref = m_venue.injectSynthetic(side, tick, qty, ts); });
        return ref;
    }
    void cancelSynthetic(lob::OrderId ref, std::uint64_t ts) {
        withMarketData([&] { m_venue.cancelSynthetic(ref, ts); });
    }
    void sessionEvent(itch::SystemEventCode code, std::uint64_t ts) {
        withMarketData([&] { m_venue.sessionEvent(code, ts); });
    }

    [[nodiscard]] lob::Price bestBid() const noexcept { return m_venue.bestBid(); }
    [[nodiscard]] lob::Price bestAsk() const noexcept { return m_venue.bestAsk(); }
    [[nodiscard]] const lob::OrderBook& book() const noexcept { return m_venue.book(); }

    struct VenueSink {
        ExchangeSession* s;
        void marketData(std::span<const std::byte> itch) { s->appendMarketData(itch); }
        void orderEntry(std::span<const std::byte> ouch) { s->sendOrderEntry(ouch); }
    };

private:
    void handleSoup(const soup::Packet& p, std::uint64_t ts) {
        switch (p.type) {
        case soup::Type::LoginRequest: {
            const auto pkt = soup::packLoginAccepted(m_oeBuf.data(), m_cfg.session, m_outSeq);
            m_io.orderEntryOut(pkt);
            break;
        }
        case soup::Type::UnsequencedData:
            dispatchOuch(p.payload, ts);
            break;
        case soup::Type::LogoutRequest: {
            const auto pkt = soup::packEndOfSession(m_oeBuf.data());
            m_io.orderEntryOut(pkt);
            break;
        }
        case soup::Type::ClientHeartbeat:
        default:
            break;
        }
    }

    void dispatchOuch(std::span<const std::byte> payload, std::uint64_t ts) {
        if (payload.empty()) return;
        const char t = static_cast<char>(payload[0]);
        if (t == static_cast<char>(ouch::InType::EnterOrder) &&
            payload.size() >= sizeof(ouch::EnterOrder)) {
            ouch::EnterOrder o{};
            std::memcpy(&o, payload.data(), sizeof o);
            withMarketData([&] { m_venue.onEnterOrder(o, ts); });
        } else if (t == static_cast<char>(ouch::InType::CancelOrder) &&
                   payload.size() >= sizeof(ouch::CancelOrder)) {
            ouch::CancelOrder x{};
            std::memcpy(&x, payload.data(), sizeof x);
            withMarketData([&] { m_venue.onCancelOrder(x, ts); });
        } else if (t == static_cast<char>(ouch::InType::ReplaceOrder) &&
                   payload.size() >= sizeof(ouch::ReplaceOrder)) {
            ouch::ReplaceOrder u{};
            std::memcpy(&u, payload.data(), sizeof u);
            withMarketData([&] { m_venue.onReplaceOrder(u, ts); });
        }
    }

    template <class Fn>
    void withMarketData(Fn&& fn) {
        fn();
        flushMarketData();
    }

    void appendMarketData(std::span<const std::byte> itch) {
        if (!m_mdOpen) {
            m_packer.reset(m_mdBuf.data(), m_mdBuf.size());
            m_mdOpen = true;
        }
        if (!m_packer.append(itch)) {
            flushMarketData();
            m_packer.reset(m_mdBuf.data(), m_mdBuf.size());
            m_mdOpen = true;
            (void)m_packer.append(itch);
        }
    }
    void flushMarketData() {
        if (m_mdOpen && m_packer.count() > 0) {
            m_io.marketDataOut(m_packer.finalize());
        }
        m_mdOpen = false;
    }

    void sendOrderEntry(std::span<const std::byte> ouch) {
        const auto pkt = soup::packSequencedData(m_oeBuf.data(), ouch);
        m_io.orderEntryOut(pkt);
        ++m_outSeq;
    }

    Io&               m_io;
    Config            m_cfg;
    VenueSink         m_sink;
    mold::Packer      m_packer;
    Venue<VenueSink>  m_venue;

    std::vector<std::byte>       m_rxBuf;
    std::array<std::byte, 2048>  m_mdBuf{};
    std::array<std::byte, 512>   m_oeBuf{};
    bool                         m_mdOpen = false;
    std::uint64_t                m_outSeq = 1;
};

}
