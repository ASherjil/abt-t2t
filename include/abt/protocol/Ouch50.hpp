#pragma once
//
// Ouch50.hpp -- Nasdaq OUCH 5.0 order-entry messages (North American Trading Services).
//
// Byte-exact wire layouts from the Nasdaq OUCH 5.0 Order Entry Specification. As with
// Itch50.hpp, every field is a big-endian / space-padded overlay primitive so each struct
// is standard-layout, alignof == 1, and sizeof == the spec's fixed length.
//
// Differences from ITCH worth noting:
//   * OUCH Price is an 8-byte unsigned Long with 4 implied decimal places (ITCH uses 4).
//   * Outbound Timestamp is an 8-byte nanoseconds-since-midnight Long (ITCH uses 48-bit).
//   * Most messages end with a 2-byte "Appendage Length" followed by a variable
//     "Optional Appendage" of concatenated TagValue (TLV) option elements (Appendix A).
//     The structs below cover the FIXED portion *through* the Appendage Length field; the
//     appendage bytes follow at sizeof(Struct) and are handled by the TagValue helpers.
//
// Implemented set (the core order lifecycle):
//   Inbound  (host -> Nasdaq):  O EnterOrder    U ReplaceOrder   X CancelOrder
//   Outbound (Nasdaq -> host):  S SystemEvent   A Accepted       U Replaced
//                               C Canceled      E Executed       J Rejected
//
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "abt/protocol/Endian.hpp"

namespace abt::ouch {

using wire::u16be;
using wire::u32be;
using wire::u64be;

using userref_t   = u32be;          // UserRefNum: day-unique, strictly increasing
using qty_t       = u32be;          // share quantities (unsigned)
using price_t     = u64be;          // Price: 8 bytes, 4 implied decimals
using timestamp_t = u64be;          // outbound Timestamp: ns since midnight
using orderref_t  = u64be;          // exchange-assigned Order Reference Number
using matchnum_t  = u64be;
using reject_t    = u16be;          // 2-byte reject reason code (Appendix C)
using symbol_t    = wire::Alpha<8>;
using clordid_t   = wire::Alpha<14>;

// Price is a numeric field with 4 implied decimals; wire / kPriceScale == currency units.
inline constexpr std::uint32_t kPriceScale  = 10000;
// Special price flagging a market order for a cross ($214,748.3647).
inline constexpr std::uint64_t kMarketPrice = 0x7FFFFFFFull;
// Maximum non-market price ($199,999.9900).
inline constexpr std::uint64_t kMaxPrice    = 1999999900ull;

// --- enumerations (single ASCII byte on the wire unless noted) ----------------------
enum class InType : char {   // inbound message type
    EnterOrder   = 'O',
    ReplaceOrder = 'U',
    CancelOrder  = 'X',
    ModifyOrder  = 'M',
};

enum class OutType : char {  // outbound message type
    SystemEvent   = 'S',
    Accepted      = 'A',
    Replaced      = 'U',
    Canceled      = 'C',
    AiqCanceled   = 'D',
    Executed      = 'E',
    BrokenTrade   = 'B',
    Rejected      = 'J',
    CancelPending = 'P',
    CancelReject  = 'I',
};

enum class Side : char { Buy = 'B', Sell = 'S', SellShort = 'T', SellShortExempt = 'E' };

enum class TimeInForce : char {
    Day        = '0',   // market hours
    IOC        = '3',
    GTX        = '5',   // extended hours
    GTT        = '6',   // expire time required
    AfterHours = 'E',
};

enum class Display : char { Visible = 'Y', Hidden = 'N', Attributable = 'A', Conformant = 'Z' };

enum class Capacity : char { Agency = 'A', Principal = 'P', Riskless = 'R', Other = 'O' };

enum class ImSweep : char { Eligible = 'Y', NotEligible = 'N' };

enum class CrossType : char {
    Continuous      = 'N',
    OpeningCross    = 'O',
    ClosingCross    = 'C',
    HaltIpo         = 'H',
    Supplemental    = 'S',
    Retail          = 'R',
    ExtendedLife    = 'E',
    AfterHoursClose = 'A',
};

enum class OrderState : char { Live = 'L', Dead = 'D' };

enum class EventCode : char { StartOfDay = 'S', EndOfDay = 'E' };

// Order Cancel Reasons (Appendix B).
enum class CancelReason : char {
    Regulatory          = 'D',
    Closed              = 'E',
    PostOnlyNms         = 'F',
    PostOnlyContra      = 'G',
    Halted              = 'H',
    Ioc                 = 'I',
    MarketCollars       = 'K',
    SelfMatchPrevention = 'Q',
    Supervisory         = 'S',
    Timeout             = 'T',
    UserRequested       = 'U',
    OpenProtection      = 'X',
    SystemCancel        = 'Z',
};

// Optional-attribute tags carried in the appendage (Appendix A).
enum class OptionTag : std::uint8_t {
    SecondaryOrdRefNum  = 1,
    Firm                = 2,
    MinQty              = 3,
    CustomerType        = 4,
    MaxFloor            = 5,
    PriceType           = 6,
    PegOffset           = 7,
    DiscretionPrice     = 9,
    DiscretionPriceType = 10,
    DiscretionPegOffset = 11,
    PostOnly            = 12,
    RandomReserves      = 13,
    Route               = 14,
    ExpireTime          = 15,
    TradeNow            = 16,
    HandleInst          = 17,
    BboWeightIndicator  = 18,
    DisplayQuantity     = 22,
    DisplayPrice        = 23,
    GroupId             = 24,
    SharesLocated       = 25,
    LocateBroker        = 26,
    Side                = 27,
    UserRefIdx          = 28,
};

// ===========================================================================
// Inbound messages (host -> Nasdaq).
// ===========================================================================

// 'O' Enter Order. Fixed portion 47 bytes (+ appendage).
struct EnterOrder {
    char        type;                 // 'O'
    userref_t   userRefNum;
    char        side;                 // Side
    qty_t       quantity;
    symbol_t    symbol;
    price_t     price;
    char        timeInForce;          // TimeInForce
    char        display;              // Display
    char        capacity;             // Capacity
    char        imSweepEligibility;   // ImSweep
    char        crossType;            // CrossType
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(EnterOrder) == 47);

// 'U' Replace Order Request. Fixed portion 40 bytes (+ appendage).
struct ReplaceOrder {
    char        type;                 // 'U'
    userref_t   origUserRefNum;
    userref_t   userRefNum;           // replacement UserRefNum
    qty_t       quantity;
    price_t     price;
    char        timeInForce;
    char        display;
    char        imSweepEligibility;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(ReplaceOrder) == 40);

// 'X' Cancel Order Request. Fixed portion 11 bytes (appendage optional).
struct CancelOrder {
    char        type;                 // 'X'
    userref_t   userRefNum;
    qty_t       quantity;             // new intended order size; 0 = cancel remaining
    u16be       appendageLength;
};
static_assert(sizeof(CancelOrder) == 11);

// ===========================================================================
// Outbound messages (Nasdaq -> host).
// ===========================================================================

// 'S' System Event. 10 bytes (no appendage).
struct SystemEvent {
    char        type;                 // 'S'
    timestamp_t timestamp;
    char        eventCode;            // EventCode
};
static_assert(sizeof(SystemEvent) == 10);

// 'A' Order Accepted. Fixed portion 64 bytes (+ appendage).
struct Accepted {
    char        type;                 // 'A'
    timestamp_t timestamp;
    userref_t   userRefNum;
    char        side;
    qty_t       quantity;
    symbol_t    symbol;
    price_t     price;
    char        timeInForce;
    char        display;
    orderref_t  orderReferenceNumber;
    char        capacity;
    char        imSweepEligibility;
    char        crossType;
    char        orderState;           // OrderState
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(Accepted) == 64);

// 'U' Order Replaced. Fixed portion 68 bytes (+ appendage).
struct Replaced {
    char        type;                 // 'U'
    timestamp_t timestamp;
    userref_t   origUserRefNum;
    userref_t   userRefNum;
    char        side;
    qty_t       quantity;
    symbol_t    symbol;
    price_t     price;
    char        timeInForce;
    char        display;
    orderref_t  orderReferenceNumber;
    char        capacity;
    char        imSweepEligibility;
    char        crossType;
    char        orderState;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(Replaced) == 68);

// 'C' Order Canceled. Fixed portion 20 bytes (appendage optional).
struct Canceled {
    char        type;                 // 'C'
    timestamp_t timestamp;
    userref_t   userRefNum;
    qty_t       quantity;             // shares decremented (incremental, not cumulative)
    char        reason;               // CancelReason
    u16be       appendageLength;
};
static_assert(sizeof(Canceled) == 20);

// 'E' Order Executed. Fixed portion 36 bytes (+ appendage).
struct Executed {
    char        type;                 // 'E'
    timestamp_t timestamp;
    userref_t   userRefNum;
    qty_t       quantity;             // incremental shares executed
    price_t     price;                // execution price
    char        liquidityFlag;
    matchnum_t  matchNumber;
    u16be       appendageLength;
};
static_assert(sizeof(Executed) == 36);

// 'J' Rejected. Fixed portion 31 bytes (appendage optional).
struct Rejected {
    char        type;                 // 'J'
    timestamp_t timestamp;
    userref_t   userRefNum;
    reject_t    reason;               // 2-byte reason code (Appendix C)
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(Rejected) == 31);

// --- every message must be a padding-free, trivially-overlayable POD ----------------
template <class M>
inline constexpr bool is_wire_message_v =
    std::is_trivially_copyable_v<M> && std::is_standard_layout_v<M> && alignof(M) == 1;

static_assert(is_wire_message_v<EnterOrder>);
static_assert(is_wire_message_v<ReplaceOrder>);
static_assert(is_wire_message_v<CancelOrder>);
static_assert(is_wire_message_v<SystemEvent>);
static_assert(is_wire_message_v<Accepted>);
static_assert(is_wire_message_v<Replaced>);
static_assert(is_wire_message_v<Canceled>);
static_assert(is_wire_message_v<Executed>);
static_assert(is_wire_message_v<Rejected>);

// ===========================================================================
// Optional Appendage: a run of concatenated TagValue (TLV) elements.
//   [Length:1][OptionTag:1][OptionValue:(Length-1)]
// where Length is the remaining length of the element (tag byte + value bytes).
// ===========================================================================

// Append one option; returns bytes written (0 if `out` is too small). `value` is the raw
// big-endian option payload (its width per Appendix A).
inline std::size_t writeOption(std::span<std::byte> out, OptionTag tag,
                               std::span<const std::byte> value) noexcept {
    const std::size_t total = 2 + value.size();
    if (out.size() < total) {
        return 0;
    }
    out[0] = static_cast<std::byte>(1 + value.size());   // remaining length: tag + value
    out[1] = static_cast<std::byte>(tag);
    std::memcpy(out.data() + 2, value.data(), value.size());
    return total;
}

// Iterate the TagValue elements in an appendage, invoking fn(OptionTag, value-span).
// Stops cleanly on a malformed/short element.
template <class Fn>
void forEachOption(std::span<const std::byte> appendage, Fn&& fn) {
    std::size_t i = 0;
    while (i + 1 < appendage.size()) {
        const std::size_t len = std::to_integer<std::size_t>(appendage[i]);
        if (len == 0 || i + 1 + len > appendage.size()) {
            break;   // zero length or runs past the buffer -> malformed / end
        }
        const auto tag = static_cast<OptionTag>(std::to_integer<std::uint8_t>(appendage[i + 1]));
        fn(tag, appendage.subspan(i + 2, len - 1));
        i += 1 + len;
    }
}

}  // namespace abt::ouch
