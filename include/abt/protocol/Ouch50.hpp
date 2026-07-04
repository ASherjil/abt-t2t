#pragma once
//
// Nasdaq OUCH 5.0 order-entry message layouts and TagValue appendage helpers.
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

using userref_t   = u32be;
using qty_t       = u32be;
using price_t     = u64be;
using timestamp_t = u64be;
using orderref_t  = u64be;
using matchnum_t  = u64be;
using reject_t    = u16be;
using symbol_t    = wire::Alpha<8>;
using clordid_t   = wire::Alpha<14>;

inline constexpr std::uint32_t kPriceScale  = 10000;
inline constexpr std::uint64_t kMarketPrice = 0x7FFFFFFFull;
inline constexpr std::uint64_t kMaxPrice    = 1999999900ull;

enum class InType : char {
    EnterOrder   = 'O',
    ReplaceOrder = 'U',
    CancelOrder  = 'X',
    ModifyOrder  = 'M',
};

enum class OutType : char {
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
    Day        = '0',
    IOC        = '3',
    GTX        = '5',
    GTT        = '6',
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

struct EnterOrder {
    char        type;
    userref_t   userRefNum;
    char        side;
    qty_t       quantity;
    symbol_t    symbol;
    price_t     price;
    char        timeInForce;
    char        display;
    char        capacity;
    char        imSweepEligibility;
    char        crossType;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(EnterOrder) == 47);

struct ReplaceOrder {
    char        type;
    userref_t   origUserRefNum;
    userref_t   userRefNum;
    qty_t       quantity;
    price_t     price;
    char        timeInForce;
    char        display;
    char        imSweepEligibility;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(ReplaceOrder) == 40);

struct CancelOrder {
    char        type;
    userref_t   userRefNum;
    qty_t       quantity;
    u16be       appendageLength;
};
static_assert(sizeof(CancelOrder) == 11);

struct SystemEvent {
    char        type;
    timestamp_t timestamp;
    char        eventCode;
};
static_assert(sizeof(SystemEvent) == 10);

struct Accepted {
    char        type;
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
    char        orderState;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(Accepted) == 64);

struct Replaced {
    char        type;
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

struct Canceled {
    char        type;
    timestamp_t timestamp;
    userref_t   userRefNum;
    qty_t       quantity;
    char        reason;
    u16be       appendageLength;
};
static_assert(sizeof(Canceled) == 20);

struct Executed {
    char        type;
    timestamp_t timestamp;
    userref_t   userRefNum;
    qty_t       quantity;
    price_t     price;
    char        liquidityFlag;
    matchnum_t  matchNumber;
    u16be       appendageLength;
};
static_assert(sizeof(Executed) == 36);

struct Rejected {
    char        type;
    timestamp_t timestamp;
    userref_t   userRefNum;
    reject_t    reason;
    clordid_t   clOrdId;
    u16be       appendageLength;
};
static_assert(sizeof(Rejected) == 31);

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

inline std::size_t writeOption(std::span<std::byte> out, OptionTag tag,
                               std::span<const std::byte> value) noexcept {
    const std::size_t total = 2 + value.size();
    if (out.size() < total) {
        return 0;
    }
    out[0] = static_cast<std::byte>(1 + value.size());
    out[1] = static_cast<std::byte>(tag);
    std::memcpy(out.data() + 2, value.data(), value.size());
    return total;
}

template <class Fn>
void forEachOption(std::span<const std::byte> appendage, Fn&& fn) {
    std::size_t i = 0;
    while (i + 1 < appendage.size()) {
        const std::size_t len = std::to_integer<std::size_t>(appendage[i]);
        if (len == 0 || i + 1 + len > appendage.size()) {
            break;
        }
        const auto tag = static_cast<OptionTag>(std::to_integer<std::uint8_t>(appendage[i + 1]));
        fn(tag, appendage.subspan(i + 2, len - 1));
        i += 1 + len;
    }
}

}
