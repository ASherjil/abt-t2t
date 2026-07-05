#pragma once
//
// DUT strategy contract. A strategy inspects the freshly-updated book after each market-data
// packet and optionally returns one order to send. It is a compile-time policy (template
// parameter of DutSession) so the react step inlines with zero indirection — the real, latency-
// critical logic lands in a later chunk; this header only fixes the shape it must conform to.
//

#include <concepts>

#include "abt/dut/BookBuilder.hpp"
#include "abt/lob/Types.hpp"

namespace abt::dut {

// One order the strategy wants on the wire. `send == false` means "do nothing this packet".
// `price` is a wire price (same scale the book and OUCH order entry use).
struct OrderIntent {
    bool     send  = false;
    Side     side  = Side::Buy;
    Price    price = 0;
    Quantity qty   = 0;
};

// A Strategy maps the current book to at most one order intent, cheaply and without throwing.
template <typename S>
concept Strategy = requires(S s, const BookBuilder& book) {
    { s.onBook(book) } noexcept -> std::same_as<OrderIntent>;
};

}
