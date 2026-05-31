#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/protocol/header_flyweight.h"

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
    static constexpr i32 TERM_OFFSET_FIELD_OFFSET   = 8;
    static constexpr i32 SESSION_ID_FIELD_OFFSET    = 12;
    static constexpr i32 STREAM_ID_FIELD_OFFSET     = 16;
    static constexpr i32 TERM_ID_FIELD_OFFSET       = 20;

    // --- Read helpers ---

    /// Read the frame length at the given offset using an acquire load.
    /// This pairs with frame_length_ordered (release) to ensure all header/payload
    /// writes by the publisher are visible to the reader when frame_length > 0.
    [[nodiscard]] static i32 frame_length(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_i32_ordered(offset + FRAME_LENGTH_FIELD_OFFSET);
    }

    /// Read the frame type at the given offset.
    [[nodiscard]] static u16 frame_type(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_u16(offset + TYPE_FIELD_OFFSET);
    }

    /// Read the term ID from the frame header.
    [[nodiscard]] static i32 frame_term_id(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_i32(offset + TERM_ID_FIELD_OFFSET);
    }

    /// Read the session ID from the frame header.
    [[nodiscard]] static i32 frame_session_id(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return buffer.get_i32(offset + SESSION_ID_FIELD_OFFSET);
    }

    /// Determine whether the frame at the given offset is a padding frame.
    /// Padding frames are identified by their type field being HDR_TYPE_PAD.
    /// Note: during in-progress writes, frame_length may be negative (partial claim).
    /// After gap-filling or unblocking, padding frames have positive frame_length.
    [[nodiscard]] static bool is_padding_frame(const concurrent::UnsafeBuffer& buffer, i32 offset) noexcept
    {
        return frame_type(buffer, offset) == protocol::HeaderFlyweight::HDR_TYPE_PAD;
    }

    // --- Write helpers ---

    /// Write the frame length with release semantics. This is the publication
    /// barrier — all prior writes (payload, header fields) are guaranteed to be
    /// visible to any thread that reads this frame_length > 0 with acquire semantics.
    static void frame_length_ordered(concurrent::UnsafeBuffer& buffer, i32 offset, i32 frame_length) noexcept
    {
        buffer.put_i32_ordered(offset + FRAME_LENGTH_FIELD_OFFSET, frame_length);
    }

    /// Write the frame type field.
    static void frame_type(concurrent::UnsafeBuffer& buffer, i32 offset, u16 type) noexcept
    {
        buffer.put_u16(offset + TYPE_FIELD_OFFSET, type);
    }

    /// Write the flags field.
    static void frame_flags(concurrent::UnsafeBuffer& buffer, i32 offset, u8 flags) noexcept
    {
        buffer.put_u8(offset + FLAGS_FIELD_OFFSET, flags);
    }

    /// Write the term offset field.
    static void frame_term_offset(concurrent::UnsafeBuffer& buffer, i32 offset, i32 term_offset) noexcept
    {
        buffer.put_i32(offset + TERM_OFFSET_FIELD_OFFSET, term_offset);
    }

    /// Write the term ID field.
    static void frame_term_id(concurrent::UnsafeBuffer& buffer, i32 offset, i32 term_id) noexcept
    {
        buffer.put_i32(offset + TERM_ID_FIELD_OFFSET, term_id);
    }

    /// Write the session ID field.
    static void frame_session_id(concurrent::UnsafeBuffer& buffer, i32 offset, i32 session_id) noexcept
    {
        buffer.put_i32(offset + SESSION_ID_FIELD_OFFSET, session_id);
    }
};

} // namespace caeron::logbuffer
