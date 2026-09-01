#pragma once

#include <concepts>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/Quote.hpp"

namespace abt::dut {

template <typename S>
concept Strategy = requires (S s, const BookBuilder& book, const Account& acct) {
    {
        s.onBook(book, acct)
    } noexcept -> std::same_as<QuoteTargets>;
};

}   // namespace abt::dut
