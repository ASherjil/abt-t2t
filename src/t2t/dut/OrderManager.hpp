#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "t2t/dut/Quote.hpp"
#include "t2t/lob/Types.hpp"
#include "t2t/protocol/Ouch50.hpp"

namespace abt::dut {

struct OmsConfig {
    std::vector<std::string> symbols;
    std::uint32_t            firstUserRef = 1;
};

enum class QuoteState : std::uint8_t {
    Idle,
    PendingNew,
    Live,
    PendingReplace,
    PendingCancel
};

struct QuoteSlot {
    QuoteState    state      = QuoteState::Idle;
    std::uint32_t userRef    = 0;
    std::uint32_t pendingRef = 0;
    Price         price      = 0;
    Quantity      qty        = 0;
    Quantity      leaves     = 0;
};

struct OmsStats {
    std::uint64_t enters   = 0;
    std::uint64_t replaces = 0;
    std::uint64_t cancels  = 0;
    std::uint64_t accepts  = 0;
    std::uint64_t fills    = 0;
    std::uint64_t rejects  = 0;
    std::uint64_t unknown  = 0;
    std::uint64_t tests    = 0;
};

struct Outbound {
    static constexpr std::size_t kSize = 64;
    std::array<std::byte, kSize> buf{};
    std::size_t                  len     = 0;
    std::uint32_t                userRef = 0;
};

class OrderManager {
public:
    static constexpr std::size_t      kMaxOutbound = 2;
    static constexpr std::uint16_t    kTestSym     = 0xffffu;
    static constexpr std::string_view kTestSymbol  = "ZVZZT";

    explicit OrderManager(const OmsConfig& cfg);

    std::size_t reconcile(std::size_t sym, const QuoteTargets& t,
                          std::span<Outbound, kMaxOutbound> out) noexcept;
    void        onAck(std::span<const std::byte> ouch) noexcept;
    void        encodeTestOrder(Outbound& out) noexcept;
    void        prefetch(std::size_t sym) const noexcept;

    [[nodiscard]] std::size_t      symbolCount() const noexcept;
    [[nodiscard]] const Account&   account(std::size_t sym = 0) const noexcept;
    [[nodiscard]] std::int64_t     netPosition() const noexcept;
    [[nodiscard]] const QuoteSlot& slot(std::size_t sym, Side side) const noexcept;
    [[nodiscard]] const QuoteSlot& slot(Side side) const noexcept;
    [[nodiscard]] const OmsStats&  stats() const noexcept;
    [[nodiscard]] std::uint32_t    nextUserRef() const noexcept;

private:
    static constexpr std::size_t kRefRing = 4096;

    struct RefSide {
        std::uint32_t userRef = 0;
        std::uint16_t sym     = 0;
        Side          side    = Side::Buy;
    };

    using Pair = std::array<QuoteSlot, 2>;

    struct EnterTemplates {
        ouch::EnterOrder bid{};
        ouch::EnterOrder ask{};
    };

    [[nodiscard]] static ouch::EnterOrder   enterTemplate(std::string_view symbol, Side side) noexcept;
    [[nodiscard]] static ouch::ReplaceOrder replaceTemplate() noexcept;

    [[nodiscard]] std::size_t reconcileSide(std::size_t sym, Side side, bool want, Price price, Quantity qty,
                                            Outbound& out) noexcept;
    [[nodiscard]] std::uint32_t allocRef(std::size_t sym, Side side) noexcept;
    [[nodiscard]] bool          lookupRef(std::uint32_t userRef, std::size_t& sym, Side& side) const noexcept;
    [[nodiscard]] QuoteSlot*    slotByRef(std::uint32_t userRef, std::size_t& sym) noexcept;
    [[nodiscard]] bool          isTestRef(std::uint32_t userRef) const noexcept;
    [[nodiscard]] QuoteSlot*    slotByPending(std::uint32_t userRef) noexcept;

    void        encodeEnter(Outbound& out, std::size_t sym, std::uint32_t userRef, Side side, Price price,
                            Quantity qty) const noexcept;
    void        encodeReplace(Outbound& out, std::uint32_t origRef, std::uint32_t userRef, Price price,
                              Quantity qty) const noexcept;
    static void encodeCancel(Outbound& out, std::uint32_t userRef) noexcept;

    void        onAccepted(const ouch::Accepted& m) noexcept;
    void        onReplaced(const ouch::Replaced& m) noexcept;
    void        onExecuted(const ouch::Executed& m) noexcept;
    void        onCanceled(const ouch::Canceled& m) noexcept;
    void        onRejected(const ouch::Rejected& m) noexcept;
    void        onCancelReject(const ouch::CancelReject& m) noexcept;
    static void settle(QuoteSlot& s) noexcept;

    OmsConfig                     m_cfg;
    OmsStats                      m_stats{};
    std::uint32_t                 m_nextUserRef;
    std::vector<Pair>             m_slots;
    std::vector<Account>          m_acct;
    std::array<RefSide, kRefRing> m_refs{};
    std::vector<EnterTemplates>   m_enter;
    ouch::ReplaceOrder            m_replace{};
};

}   // namespace abt::dut
