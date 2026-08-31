#pragma once

#include <concepts>

#include "abt/dut/BookBuilder.hpp"
#include "abt/dut/Quote.hpp"

namespace abt::dut {

template <typename S>
concept Strategy = requires (S s, const BookBuilder& book, const Account& acct) {
    {
        s.onBook(book, acct)
    } noexcept -> std::same_as<QuoteTargets>;
};

}   // namespace abt::dut
