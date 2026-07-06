#pragma once
//
// Realistic market-making quoter. Each market-data update it recomputes a fair value from the size-
// weighted micro-price (leans toward the side with less resting size, i.e. more pressure), places a
// two-sided quote a configured half-spread wide around it, and shifts both quotes by an inventory
// skew — long inventory pushes the quotes down so the DUT leans net seller, short pushes them up.
// Prices are rounded to the tick grid (bid down, ask up), kept from crossing, and clamped to the
// band. This is the reservation-price / inventory-skew behaviour real MM firms run (Avellaneda-
// Stoikov-style), minus hard position limits. Split .hpp/.cpp per project style.
//

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/Quote.hpp"
#include "abt/lob/Types.hpp"

namespace abt::dut {

struct QuoterConfig {
    Price    tickWire         = 1;     // price granularity
    Price    halfSpreadTicks  = 1;     // half of the quoted spread, in ticks
    Quantity quoteQty         = 100;   // size per side
    double   skewTicksPerUnit = 0.0;   // quote shift (in ticks) per share of inventory
    Price    minPrice         = 0;     // band clamp
    Price    maxPrice         = 0;
};

class QuoterStrategy {
public:
    explicit QuoterStrategy(const QuoterConfig& cfg) noexcept;

    [[nodiscard]] QuoteTargets onBook(const BookBuilder& book, const Account& acct) noexcept;

private:
    [[nodiscard]] Price roundDownToTick(double price) const noexcept;
    [[nodiscard]] Price roundUpToTick(double price) const noexcept;
    [[nodiscard]] Price clampToBand(Price price) const noexcept;

    QuoterConfig m_cfg;
};

}
