#pragma once

#include "types.h"

#include <bit>
#include <cstring>

namespace caeron {

static_assert(std::endian::native == std::endian::little,
              "Caeron requires a little-endian platform");

[[nodiscard]] constexpr u16 to_le(u16 v) noexcept { return v; }
[[nodiscard]] constexpr u32 to_le(u32 v) noexcept { return v; }
[[nodiscard]] constexpr u64 to_le(u64 v) noexcept { return v; }
[[nodiscard]] constexpr i16 to_le(i16 v) noexcept { return v; }
[[nodiscard]] constexpr i32 to_le(i32 v) noexcept { return v; }
[[nodiscard]] constexpr i64 to_le(i64 v) noexcept { return v; }

[[nodiscard]] constexpr u16 from_le(u16 v) noexcept { return v; }
[[nodiscard]] constexpr u32 from_le(u32 v) noexcept { return v; }
[[nodiscard]] constexpr u64 from_le(u64 v) noexcept { return v; }
[[nodiscard]] constexpr i16 from_le(i16 v) noexcept { return v; }
[[nodiscard]] constexpr i32 from_le(i32 v) noexcept { return v; }
[[nodiscard]] constexpr i64 from_le(i64 v) noexcept { return v; }

} // namespace caeron
