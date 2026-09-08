#pragma once

#include <concepts>

#include "t2t/dut/BookBuilder.hpp"
#include "t2t/dut/OrderManager.hpp"

namespace abt::dut {

template <typename S>
concept Strategy = requires (S s, const BookBuilder& book, const Account& acct, QuoteTargets& out) {
    {
        s.onBook(book, acct, out)
    } noexcept -> std::same_as<bool>;
    {
        s.forget()
    } noexcept;
};

}   // namespace abt::dut
