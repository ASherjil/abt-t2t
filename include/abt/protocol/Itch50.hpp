#pragma once
//
// Itch50.hpp -- Nasdaq TotalView-ITCH 5.0 outbound market-data messages.
//
// These are the byte-exact wire layouts published in the Nasdaq TotalView-ITCH 5.0
// spec. Every field is a big-endian / space-padded overlay primitive from Endian.hpp,
// so each struct is standard-layout with alignof == 1 and sizeof == the spec length --
// it can be laid directly over a received frame with zero copies.
//
// A message as it travels the wire is prefixed by a 2-byte big-endian length inside a
// MoldUDP64 message block; the structs below are the message *bodies* only (the byte
// starting at the 1-byte message type). MoldUDP64 framing lives in a separate header.
//
// Prices are ITCH "Price(4)": a 4-byte unsigned integer with 4 implied decimal places
// (wire value / 10000 == price in currency units). Timestamps are 48-bit nanoseconds
// since midnight (Uint48).
//
// Implemented set (the core book-building + reference messages):
//   S StockDirectory-adjacent SystemEvent   R StockDirectory       H StockTradingAction
//   A AddOrder            F AddOrderMpid     E OrderExecuted        C OrderExecutedWithPrice
//   X OrderCancel         D OrderDelete      U OrderReplace
//   P TradeNonCross       Q CrossTrade
//
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "abt/protocol/Endian.hpp"

namespace abt::itch {

using wire::u16be;
using wire::u32be;
using wire::u64be;
using wire::Uint48;
using stock_t = wire::Alpha<8>;

// Price(4) fixed-point scale: wire integer / kPriceScale == price in currency units.
inline constexpr std::uint32_t kPriceScale = 10000;

// --- enumerations (each is a single ASCII byte on the wire) -------------------------
enum class MessageType : char {
    SystemEvent            = 'S',
    StockDirectory         = 'R',
    StockTradingAction     = 'H',
    AddOrder               = 'A',
    AddOrderMpid           = 'F',
    OrderExecuted          = 'E',
    OrderExecutedWithPrice = 'C',
    OrderCancel            = 'X',
    OrderDelete            = 'D',
    OrderReplace           = 'U',
    TradeNonCross          = 'P',
    CrossTrade             = 'Q',
};

enum class Side : char { Buy = 'B', Sell = 'S' };

enum class SystemEventCode : char {
    StartOfMessages     = 'O',
    StartOfSystemHours  = 'S',
    StartOfMarketHours  = 'Q',
    EndOfMarketHours    = 'M',
    EndOfSystemHours    = 'E',
    EndOfMessages       = 'C',
};

enum class TradingState : char {
    Halted        = 'H',
    Paused        = 'P',
    QuotationOnly = 'Q',
    Trading       = 'T',
};

enum class CrossType : char {
    Opening     = 'O',
    Closing     = 'C',
    Halted      = 'H',
    Intraday    = 'I',  // intraday / post-close extended
};

// ---------------------------------------------------------------------------
// Messages. All fields are declared in wire order; the common 10-byte header
// (type, stockLocate, trackingNumber, timestamp) opens every message.
// ---------------------------------------------------------------------------

// 'S' System Event -- session lifecycle markers. 12 bytes.
struct SystemEvent {
    char   messageType;      // 'S'
    u16be  stockLocate;
    u16be  trackingNumber;
    Uint48 timestamp;
    char   eventCode;        // SystemEventCode
};
static_assert(sizeof(SystemEvent) == 12);

// 'R' Stock Directory -- per-symbol reference data (maps stockLocate <-> symbol). 39 bytes.
struct StockDirectory {
    char    messageType;     // 'R'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    stock_t stock;
    char    marketCategory;
    char    financialStatusIndicator;
    u32be   roundLotSize;
    char    roundLotsOnly;
    char    issueClassification;
    wire::Alpha<2> issueSubType;
    char    authenticity;
    char    shortSaleThresholdIndicator;
    char    ipoFlag;
    char    luldReferencePriceTier;
    char    etpFlag;
    u32be   etpLeverageFactor;
    char    inverseIndicator;
};
static_assert(sizeof(StockDirectory) == 39);

// 'H' Stock Trading Action -- halts / resumes. 25 bytes.
struct StockTradingAction {
    char    messageType;     // 'H'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    stock_t stock;
    char    tradingState;    // TradingState
    char    reserved;
    wire::Alpha<4> reason;
};
static_assert(sizeof(StockTradingAction) == 25);

// 'A' Add Order (no attribution) -- a new displayable resting order. 36 bytes.
struct AddOrder {
    char    messageType;     // 'A'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;        // order reference number (book-unique)
    char    side;            // Side ('B' / 'S')
    u32be   shares;
    stock_t stock;
    u32be   price;           // Price(4)
};
static_assert(sizeof(AddOrder) == 36);

// 'F' Add Order with MPID Attribution. 40 bytes ( = AddOrder + 4-byte attribution).
struct AddOrderMpid {
    char    messageType;     // 'F'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    char    side;
    u32be   shares;
    stock_t stock;
    u32be   price;
    wire::Alpha<4> attribution;
};
static_assert(sizeof(AddOrderMpid) == 40);

// 'E' Order Executed -- resting order (partially) executed at its display price. 31 bytes.
struct OrderExecuted {
    char    messageType;     // 'E'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   executedShares;
    u64be   matchNumber;
};
static_assert(sizeof(OrderExecuted) == 31);

// 'C' Order Executed With Price -- execution at a price other than display. 36 bytes.
struct OrderExecutedWithPrice {
    char    messageType;     // 'C'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   executedShares;
    u64be   matchNumber;
    char    printable;       // 'Y' / 'N' (contributes to consolidated tape?)
    u32be   executionPrice;  // Price(4)
};
static_assert(sizeof(OrderExecutedWithPrice) == 36);

// 'X' Order Cancel -- shares cancelled from a resting order (partial). 23 bytes.
struct OrderCancel {
    char    messageType;     // 'X'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   cancelledShares;
};
static_assert(sizeof(OrderCancel) == 23);

// 'D' Order Delete -- resting order fully removed. 19 bytes.
struct OrderDelete {
    char    messageType;     // 'D'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
};
static_assert(sizeof(OrderDelete) == 19);

// 'U' Order Replace -- cancel origOrderRef, add newOrderRef with new size/price. 35 bytes.
struct OrderReplace {
    char    messageType;     // 'U'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   origOrderRef;
    u64be   newOrderRef;
    u32be   shares;
    u32be   price;           // Price(4)
};
static_assert(sizeof(OrderReplace) == 35);

// 'P' Trade (non-cross) -- execution against a non-displayable order. 44 bytes.
struct TradeNonCross {
    char    messageType;     // 'P'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    char    side;
    u32be   shares;
    stock_t stock;
    u32be   price;           // Price(4)
    u64be   matchNumber;
};
static_assert(sizeof(TradeNonCross) == 44);

// 'Q' Cross Trade -- opening / closing / halt cross print. 40 bytes.
struct CrossTrade {
    char    messageType;     // 'Q'
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   shares;          // 8-byte cross quantity
    stock_t stock;
    u32be   crossPrice;      // Price(4)
    u64be   matchNumber;
    char    crossType;       // CrossType
};
static_assert(sizeof(CrossTrade) == 40);

// --- every message must be a padding-free, trivially-overlayable POD ----------------
template <class M>
inline constexpr bool is_wire_message_v =
    std::is_trivially_copyable_v<M> && std::is_standard_layout_v<M> && alignof(M) == 1;

static_assert(is_wire_message_v<SystemEvent>);
static_assert(is_wire_message_v<StockDirectory>);
static_assert(is_wire_message_v<StockTradingAction>);
static_assert(is_wire_message_v<AddOrder>);
static_assert(is_wire_message_v<AddOrderMpid>);
static_assert(is_wire_message_v<OrderExecuted>);
static_assert(is_wire_message_v<OrderExecutedWithPrice>);
static_assert(is_wire_message_v<OrderCancel>);
static_assert(is_wire_message_v<OrderDelete>);
static_assert(is_wire_message_v<OrderReplace>);
static_assert(is_wire_message_v<TradeNonCross>);
static_assert(is_wire_message_v<CrossTrade>);

}  // namespace abt::itch
