#pragma once

#include "types.h"

#include <bit>
#include <stdexcept>

namespace caeron {

inline constexpr i32 CACHE_LINE_LENGTH = 64;
inline constexpr i32 SIZE_OF_SHORT = 2;
inline constexpr i32 SIZE_OF_INT = 4;
inline constexpr i32 SIZE_OF_LONG = 8;

[[nodiscard]] constexpr bool is_power_of_two(i32 value) noexcept
{
    return value > 0 && std::has_single_bit(static_cast<u32>(value));
}

[[nodiscard]] constexpr i32 align(i32 value, i32 alignment)
{
    if (!is_power_of_two(alignment))
        throw std::invalid_argument("alignment must be a power of two");
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] constexpr i32 next_power_of_two(i32 value)
{
    if (value <= 0)
        throw std::invalid_argument("value must be positive");
    return static_cast<i32>(std::bit_ceil(static_cast<u32>(value)));
}

} // namespace caeron
