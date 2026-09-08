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

#include <limits>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/OrderManager.hpp"
#include "t2t/lob/Types.hpp"

namespace abt::dut {

struct QuoterConfig {
    Price    tickWire         = 1;     // price granularity
    Price    halfSpreadTicks  = 1;     // half of the quoted spread, in ticks
    Quantity quoteQty         = 100;   // size per side
    double   skewTicksPerUnit = 0.0;   // quote shift (in ticks) per share of inventory
};

class QuoterStrategy {
public:
    explicit QuoterStrategy(const QuoterConfig& cfg) noexcept;

    [[nodiscard]] bool onBook(const BookBuilder& book, const Account& acct, QuoteTargets& out) noexcept;
    void               forget() noexcept;

private:
    [[nodiscard]] Price        roundDownToTick(double price, Price origin) const noexcept;
    [[nodiscard]] Price        roundUpToTick(double price, Price origin) const noexcept;
    [[nodiscard]] static Price clampToBand(Price price, const BookBuilder& book) noexcept;

    QuoterConfig m_cfg;
    double       m_fairLo = 0.0;
    double       m_fairHi = -1.0;
};

}   // namespace abt::dut
