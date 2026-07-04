#pragma once
//
// Nasdaq TotalView-ITCH 5.0 market-data message layouts (byte-exact overlay structs).
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

inline constexpr std::uint32_t kPriceScale = 10000;

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
    Intraday    = 'I',
};

struct SystemEvent {
    char   messageType;
    u16be  stockLocate;
    u16be  trackingNumber;
    Uint48 timestamp;
    char   eventCode;
};
static_assert(sizeof(SystemEvent) == 12);

struct StockDirectory {
    char    messageType;
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

struct StockTradingAction {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    stock_t stock;
    char    tradingState;
    char    reserved;
    wire::Alpha<4> reason;
};
static_assert(sizeof(StockTradingAction) == 25);

struct AddOrder {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    char    side;
    u32be   shares;
    stock_t stock;
    u32be   price;
};
static_assert(sizeof(AddOrder) == 36);

struct AddOrderMpid {
    char    messageType;
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

struct OrderExecuted {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   executedShares;
    u64be   matchNumber;
};
static_assert(sizeof(OrderExecuted) == 31);

struct OrderExecutedWithPrice {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   executedShares;
    u64be   matchNumber;
    char    printable;
    u32be   executionPrice;
};
static_assert(sizeof(OrderExecutedWithPrice) == 36);

struct OrderCancel {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    u32be   cancelledShares;
};
static_assert(sizeof(OrderCancel) == 23);

struct OrderDelete {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
};
static_assert(sizeof(OrderDelete) == 19);

struct OrderReplace {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   origOrderRef;
    u64be   newOrderRef;
    u32be   shares;
    u32be   price;
};
static_assert(sizeof(OrderReplace) == 35);

struct TradeNonCross {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   orderRef;
    char    side;
    u32be   shares;
    stock_t stock;
    u32be   price;
    u64be   matchNumber;
};
static_assert(sizeof(TradeNonCross) == 44);

struct CrossTrade {
    char    messageType;
    u16be   stockLocate;
    u16be   trackingNumber;
    Uint48  timestamp;
    u64be   shares;
    stock_t stock;
    u32be   crossPrice;
    u64be   matchNumber;
    char    crossType;
};
static_assert(sizeof(CrossTrade) == 40);

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

}
