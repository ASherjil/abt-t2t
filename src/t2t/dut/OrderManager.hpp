#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "t2t/dut/Quote.hpp"
#include "t2t/lob/Types.hpp"
#include "t2t/protocol/Ouch50.hpp"

namespace abt::dut {

struct OmsConfig {
    std::string   symbol{};
    std::uint32_t firstUserRef = 1;
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
};

struct Outbound {
    static constexpr std::size_t kSize = 64;
    std::array<std::byte, kSize> buf{};
    std::size_t                  len     = 0;
    std::uint32_t                userRef = 0;
};

class OrderManager {
public:
    static constexpr std::size_t kMaxOutbound = 2;

    explicit OrderManager(const OmsConfig& cfg);

    std::size_t reconcile(const QuoteTargets& t, std::span<Outbound, kMaxOutbound> out) noexcept;
    void        onAck(std::span<const std::byte> ouch) noexcept;

    [[nodiscard]] const Account&   account() const noexcept;
    [[nodiscard]] const QuoteSlot& slot(Side side) const noexcept;
    [[nodiscard]] const OmsStats&  stats() const noexcept;
    [[nodiscard]] std::uint32_t    nextUserRef() const noexcept;

private:
    static constexpr std::size_t kRefRing = 1024;

    struct RefSide {
        std::uint32_t userRef = 0;
        Side          side    = Side::Buy;
    };

    [[nodiscard]] std::size_t   reconcileSide(Side side, bool want, Price price, Quantity qty,
                                              Outbound& out) noexcept;
    [[nodiscard]] std::uint32_t allocRef(Side side) noexcept;
    [[nodiscard]] bool          sideOf(std::uint32_t userRef, Side& side) const noexcept;
    [[nodiscard]] QuoteSlot*    slotByRef(std::uint32_t userRef) noexcept;
    [[nodiscard]] QuoteSlot*    slotByPending(std::uint32_t userRef) noexcept;

    void encodeEnter(Outbound& out, std::uint32_t userRef, Side side, Price price,
                     Quantity qty) const noexcept;
    void encodeReplace(Outbound& out, std::uint32_t origRef, std::uint32_t userRef, Price price,
                       Quantity qty) const noexcept;
    void encodeCancel(Outbound& out, std::uint32_t userRef) const noexcept;

    void onAccepted(const ouch::Accepted& m) noexcept;
    void onReplaced(const ouch::Replaced& m) noexcept;
    void onExecuted(const ouch::Executed& m) noexcept;
    void onCanceled(const ouch::Canceled& m) noexcept;
    void onRejected(const ouch::Rejected& m) noexcept;
    void onCancelReject(const ouch::CancelReject& m) noexcept;
    void settle(QuoteSlot& s) noexcept;

    OmsConfig                     m_cfg;
    Account                       m_acct{};
    OmsStats                      m_stats{};
    std::uint32_t                 m_nextUserRef;
    std::array<QuoteSlot, 2>      m_slots{};
    std::array<RefSide, kRefRing> m_refs{};
};

}   // namespace abt::dut
