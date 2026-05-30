#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <atomic>

namespace caeron::logbuffer {

/// Describes the layout of a single frame within a term buffer.
///
/// Every frame begins with a 4-byte length (which may be negative to indicate
/// a padding frame), followed by version, flags, and a 2-byte type field.
struct FrameDescriptor
{
    static constexpr i32 FRAME_ALIGNMENT    = 32;
    static constexpr i32 MAX_MESSAGE_LENGTH = 16 * 1024 * 1024; // 16 MB

    // Flag constants for fragment assembly.
    static constexpr u8 BEGIN_FRAG_FLAG = 0x80;
    static constexpr u8 END_FRAG_FLAG   = 0x40;
    static constexpr u8 UNFRAGMENTED    = BEGIN_FRAG_FLAG | END_FRAG_FLAG;

    // Field offsets within a frame.
    static constexpr i32 FRAME_LENGTH_FIELD_OFFSET = 0;
    static constexpr i32 VERSION_FIELD_OFFSET      = 4;
    static constexpr i32 FLAGS_FIELD_OFFSET         = 5;
    static constexpr i32 TYPE_FIELD_OFFSET          = 6;

    /// Read the frame length at the given offset using a volatile (relaxed atomic) load.
    [[nodiscard]] static i32 frame_length(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_i32_volatile(offset + FRAME_LENGTH_FIELD_OFFSET);
    }

    /// Read the frame type at the given offset.
    [[nodiscard]] static u16 frame_type(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_u16(offset + TYPE_FIELD_OFFSET);
    }

    /// Determine whether the frame at the given offset is a padding frame.
    /// Padding frames have a negative frame length.
    [[nodiscard]] static bool is_padding_frame(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return frame_length(buffer, offset) < 0;
    }
};

} // namespace caeron::logbuffer
