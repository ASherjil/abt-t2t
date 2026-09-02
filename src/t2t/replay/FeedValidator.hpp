#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "t2t/dut/BookTable.hpp"
#include "t2t/dut/SymbolProfile.hpp"

namespace abt::replay {

struct SymbolStats {
    std::string   name;
    std::uint64_t messages   = 0;
    std::uint64_t unknownRef = 0;
    std::uint64_t overReduce = 0;
    std::uint64_t crossed    = 0;
    std::uint64_t locked     = 0;
    std::uint64_t resumeXing = 0;
    std::uint64_t maxLive    = 0;
    Price         lastTrade  = 0;
};

struct FeedTotals {
    std::uint64_t messages   = 0;
    std::uint64_t unknownRef = 0;
    std::uint64_t overReduce = 0;
    std::uint64_t crossed    = 0;
    std::uint64_t locked     = 0;
    std::uint64_t resumeXing = 0;
    std::uint64_t outOfBand  = 0;
    std::uint64_t reanchors  = 0;
    std::uint64_t maxLive    = 0;
    std::size_t   symbols    = 0;
    std::size_t   subDollar  = 0;
};

class FeedValidator {
public:
    static constexpr std::uint64_t kResumeGraceNs = 10'000'000;

    explicit FeedValidator(const dut::BookTableConfig& cfg);

    void onMessage(std::span<const std::byte> msg);
    void traceReanchors(std::string symbol);

    [[nodiscard]] FeedTotals                      totals() const noexcept;
    [[nodiscard]] const std::vector<SymbolStats>& perSymbol() const noexcept;
    [[nodiscard]] const dut::BookTable&           books() const noexcept;
    [[nodiscard]] std::vector<dut::SymbolProfile> profiles() const;

private:
    void checkReference(std::uint16_t locate, OrderId ref, Quantity reduceBy) noexcept;

    dut::BookTable             m_books;
    std::vector<SymbolStats>   m_sym;
    std::vector<bool>          m_trading;
    std::vector<std::uint64_t> m_resumedAt;
    bool                       m_marketHours = false;
    bool                       m_afterClose  = false;
    std::uint16_t              m_traceLocate = 0xffffu;
    std::string                m_traceName;
    std::vector<std::uint32_t> m_traceCount;
    bool                       m_traceAll = false;
    std::uint64_t              m_messages = 0;
    std::uint64_t              m_liveNow  = 0;
    std::uint64_t              m_maxLive  = 0;
};

}   // namespace abt::replay
