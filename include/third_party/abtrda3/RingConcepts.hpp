//
// Created by asherjil on 3/2/26.
//

#ifndef ABTRDA3_RINGCONCEPTS_HPP
#define ABTRDA3_RINGCONCEPTS_HPP

#include "RxFrame.hpp"
#include <cstdint>
#include <span>

template <typename T>
concept TxRing = requires(T t, std::span<const std::uint8_t> frame, std::uint32_t len){
  { t.send(frame) } noexcept -> std::same_as<bool>;
  { t.acquire(len) } noexcept -> std::same_as<std::uint8_t*>;
  { t.commit() } noexcept;
  { t.prefillRing(frame) } noexcept;
};

template <typename T>
concept RxRing = requires (T t){
  { t.tryReceive() } noexcept -> std::same_as<RxFrame>;
  { t.release() } noexcept;
};

#endif // ABTRDA3_RINGCONCEPTS_HPP
