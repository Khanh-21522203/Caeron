#pragma once

#include "caeron/common/types.h"

#include <cstring>
#include <endian.h>

namespace caeron {

/// Read a little-endian u16 from an unaligned pointer.
[[nodiscard]] inline u16 get_le16(const void* ptr) noexcept
{
    u16 val;
    std::memcpy(&val, ptr, sizeof(u16));
    return le16toh(val);
}

/// Write a u16 as little-endian to an unaligned pointer.
inline void put_le16(void* ptr, u16 val) noexcept
{
    u16 le_val = htole16(val);
    std::memcpy(ptr, &le_val, sizeof(u16));
}

/// Read a little-endian i32 from an unaligned pointer.
[[nodiscard]] inline i32 get_le32(const void* ptr) noexcept
{
    u32 val;
    std::memcpy(&val, ptr, sizeof(u32));
    return static_cast<i32>(le32toh(val));
}

/// Write an i32 as little-endian to an unaligned pointer.
inline void put_le32(void* ptr, i32 val) noexcept
{
    u32 le_val = htole32(static_cast<u32>(val));
    std::memcpy(ptr, &le_val, sizeof(u32));
}

/// Read a little-endian i64 from an unaligned pointer.
[[nodiscard]] inline i64 get_le64(const void* ptr) noexcept
{
    u64 val;
    std::memcpy(&val, ptr, sizeof(u64));
    return static_cast<i64>(le64toh(val));
}

/// Write an i64 as little-endian to an unaligned pointer.
inline void put_le64(void* ptr, i64 val) noexcept
{
    u64 le_val = htole64(static_cast<u64>(val));
    std::memcpy(ptr, &le_val, sizeof(u64));
}

} // namespace caeron
