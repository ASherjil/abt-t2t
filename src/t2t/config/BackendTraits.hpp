#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include "third_party/abtrda3/RingConcepts.hpp"

#include "t2t/config/NicSpec.hpp"

namespace abt {

struct SocketBackend {};

template <class B>
concept RingBackend = TxRing<B> && RxRing<B>;

template <class T>
concept BackendTraits = requires (const NicSpec& nic, typename T::Type& b) {
    {
        T::kName
    } -> std::convertible_to<std::string_view>;
    {
        T::kMaxTxFrame
    } -> std::convertible_to<std::uint32_t>;
    {
        T::make(nic)
    } -> std::same_as<typename T::Type>;
    {
        T::init(b, nic)
    } -> std::same_as<bool>;
} && (std::same_as<typename T::Type, SocketBackend> || RingBackend<typename T::Type>);

template <class T>
inline constexpr bool kIsSocketBackend = std::same_as<typename T::Type, SocketBackend>;

}   // namespace abt
