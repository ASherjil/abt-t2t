#pragma once
//
// A minimal but realistic tick-to-trade signal: when the best offer falls to or below a trigger
// price, cross the spread once with a marketable buy at that offer, then re-arm when the offer
// lifts back above the trigger. This is the canonical signal -> trade path the harness measures —
// a single event flips the decision and an order goes out. Satisfies the Strategy concept.
//

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/Strategy.hpp"
#include "abt/lob/Types.hpp"

namespace abt::dut {

class TakeStrategy {
public:
    TakeStrategy(Price triggerAsk, Quantity qty) noexcept
        : m_triggerAsk(triggerAsk), m_qty(qty) {}

    OrderIntent onBook(const BookBuilder& book) noexcept {
        OrderIntent intent{};
        const Price ask = book.bestAsk();
        if (ask == kNoPrice) {
            return intent;
        }
        if (ask > m_triggerAsk) {
            m_armed = true;
            return intent;
        }
        if (m_armed) {
            intent = OrderIntent{true, Side::Buy, ask, m_qty};
            m_armed = false;
        }
        return intent;
    }

private:
    Price    m_triggerAsk;
    Quantity m_qty;
    bool     m_armed = true;
};

static_assert(Strategy<TakeStrategy>);

}
