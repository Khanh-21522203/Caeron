#pragma once

#include "types.h"

#include <bit>
#include <limits>
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

/// Round `value` up to the next multiple of `alignment` (which must be a power of two).
/// Returns the aligned result as i32. If the computation would overflow i32, throws
/// std::overflow_error. Use i64 intermediates to detect overflow before truncation.
[[nodiscard]] constexpr i32 align(i32 value, i32 alignment)
{
    if (!is_power_of_two(alignment))
        throw std::invalid_argument("alignment must be a power of two");
    const i64 result = (static_cast<i64>(value) + alignment - 1) & ~(static_cast<i64>(alignment) - 1);
    if (result > std::numeric_limits<i32>::max())
        throw std::overflow_error("align() overflow");
    return static_cast<i32>(result);
}

[[nodiscard]] constexpr i32 next_power_of_two(i32 value)
{
    if (value <= 0)
        throw std::invalid_argument("value must be positive");
    return static_cast<i32>(std::bit_ceil(static_cast<u32>(value)));
}

} // namespace caeron
