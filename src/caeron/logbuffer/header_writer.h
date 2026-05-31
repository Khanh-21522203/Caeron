#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::logbuffer {

/// Write a default DATA frame header into a term buffer at the given offset.
///
/// Fills in frame_length, version (0), flags (UNFRAGMENTED), type (DATA = 0x01),
/// term_offset, session_id, stream_id, and term_id following the 32-byte
/// DataHeaderFlyweight layout.
inline void write_default_header(
    concurrent::UnsafeBuffer& buffer,
    i32 term_offset,
    i32 frame_length,
    i32 session_id,
    i32 stream_id,
    i32 term_id)
{
    // DataHeaderFlyweight field offsets
    constexpr i32 FRAME_LENGTH_FIELD_OFFSET  = 0;
    constexpr i32 VERSION_FIELD_OFFSET       = 4;
    constexpr i32 FLAGS_FIELD_OFFSET          = 5;
    constexpr i32 TYPE_FIELD_OFFSET           = 6;
    constexpr i32 TERM_OFFSET_FIELD_OFFSET    = 8;
    constexpr i32 SESSION_ID_FIELD_OFFSET     = 12;
    constexpr i32 STREAM_ID_FIELD_OFFSET      = 16;
    constexpr i32 TERM_ID_FIELD_OFFSET        = 20;

    constexpr u8  CURRENT_VERSION = 0x0;
    constexpr u8  UNFRAGMENTED   = 0x80 | 0x40; // BEGIN | END
    constexpr u16 HDR_TYPE_DATA  = 0x01;

    buffer.put_i32(term_offset + FRAME_LENGTH_FIELD_OFFSET, frame_length);
    buffer.put_u8(term_offset + VERSION_FIELD_OFFSET, CURRENT_VERSION);
    buffer.put_u8(term_offset + FLAGS_FIELD_OFFSET, UNFRAGMENTED);
    buffer.put_u16(term_offset + TYPE_FIELD_OFFSET, HDR_TYPE_DATA);
    buffer.put_i32(term_offset + TERM_OFFSET_FIELD_OFFSET, term_offset);
    buffer.put_i32(term_offset + SESSION_ID_FIELD_OFFSET, session_id);
    buffer.put_i32(term_offset + STREAM_ID_FIELD_OFFSET, stream_id);
    buffer.put_i32(term_offset + TERM_ID_FIELD_OFFSET, term_id);
}

} // namespace caeron::logbuffer
