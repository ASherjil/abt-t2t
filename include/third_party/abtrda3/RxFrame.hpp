//
// Created by asherjil on 3/2/26.
//

#ifndef ABTRDA3_RXFRAME_HPP
#define ABTRDA3_RXFRAME_HPP

#include <cstdint>
#include <span>

struct RxFrame {
  std::span<const std::uint8_t> data;
  std::uint32_t sec;
  std::uint32_t nsec;
  std::uint32_t status;
};

#endif // ABTRDA3_RXFRAME_HPP
