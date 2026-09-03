#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <fmt/core.h>

#include "t2t/protocol/Itch50.hpp"

namespace abt::itch {

[[nodiscard]] inline std::string describe(std::span<const std::byte> msg) {
    if (msg.size() < sizeof(SystemEvent)) {
        return fmt::format("?? {} bytes", msg.size());
    }
    const std::byte* d    = msg.data();
    const char       type = static_cast<char>(msg[0]);
    const auto*      hdr  = reinterpret_cast<const SystemEvent*>(d);
    std::string out = fmt::format("{} loc={} ts={}", type, hdr->stockLocate.value(), hdr->timestamp.value());
    switch (static_cast<MessageType>(type)) {
        case MessageType::AddOrder:
        case MessageType::AddOrderMpid:
            if (msg.size() >= sizeof(AddOrder)) {
                const auto* a = reinterpret_cast<const AddOrder*>(d);
                out += fmt::format(" ref={} {} sh={} {} px={}", a->orderRef.value(),
                                   static_cast<char>(a->side), a->shares.value(), a->stock.view(),
                                   a->price.value());
            }
            break;
        case MessageType::OrderExecuted:
            if (msg.size() >= sizeof(OrderExecuted)) {
                const auto* e = reinterpret_cast<const OrderExecuted*>(d);
                out += fmt::format(" ref={} sh={} match={}", e->orderRef.value(), e->executedShares.value(),
                                   e->matchNumber.value());
            }
            break;
        case MessageType::OrderExecutedWithPrice:
            if (msg.size() >= sizeof(OrderExecutedWithPrice)) {
                const auto* c = reinterpret_cast<const OrderExecutedWithPrice*>(d);
                out += fmt::format(" ref={} sh={} printable={} px={}", c->orderRef.value(),
                                   c->executedShares.value(), c->printable, c->executionPrice.value());
            }
            break;
        case MessageType::OrderCancel:
            if (msg.size() >= sizeof(OrderCancel)) {
                const auto* x = reinterpret_cast<const OrderCancel*>(d);
                out += fmt::format(" ref={} sh={}", x->orderRef.value(), x->cancelledShares.value());
            }
            break;
        case MessageType::OrderDelete:
            if (msg.size() >= sizeof(OrderDelete)) {
                const auto* x = reinterpret_cast<const OrderDelete*>(d);
                out += fmt::format(" ref={}", x->orderRef.value());
            }
            break;
        case MessageType::OrderReplace:
            if (msg.size() >= sizeof(OrderReplace)) {
                const auto* u = reinterpret_cast<const OrderReplace*>(d);
                out += fmt::format(" orig={} new={} sh={} px={}", u->origOrderRef.value(),
                                   u->newOrderRef.value(), u->shares.value(), u->price.value());
            }
            break;
        case MessageType::TradeNonCross:
            if (msg.size() >= sizeof(TradeNonCross)) {
                const auto* p = reinterpret_cast<const TradeNonCross*>(d);
                out += fmt::format(" {} sh={} {} px={} match={}", static_cast<char>(p->side),
                                   p->shares.value(), p->stock.view(), p->price.value(),
                                   p->matchNumber.value());
            }
            break;
        case MessageType::CrossTrade:
            if (msg.size() >= sizeof(CrossTrade)) {
                const auto* q = reinterpret_cast<const CrossTrade*>(d);
                out += fmt::format(" sh={} {} px={} cross={}", q->shares.value(), q->stock.view(),
                                   q->crossPrice.value(), static_cast<char>(q->crossType));
            }
            break;
        case MessageType::SystemEvent:
            out += fmt::format(" event={}", static_cast<char>(hdr->eventCode));
            break;
        case MessageType::StockDirectory:
            if (msg.size() >= sizeof(StockDirectory)) {
                out += fmt::format(" {}", reinterpret_cast<const StockDirectory*>(d)->stock.view());
            }
            break;
        case MessageType::StockTradingAction:
            if (msg.size() >= sizeof(StockTradingAction)) {
                const auto* h = reinterpret_cast<const StockTradingAction*>(d);
                out += fmt::format(" {} state={}", h->stock.view(), static_cast<char>(h->tradingState));
            }
            break;
        default:
            break;
    }
    return out;
}

}   // namespace abt::itch
