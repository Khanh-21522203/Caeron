#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::logbuffer {

/// Write a default DATA frame header into a term buffer at the given offset.
///
/// Fills in frame_length, version (0), flags (UNFRAGMENTED), type (DATA = 0x01),
/// term_offset, session_id, stream_id, and term_id following the 32-byte
/// DataHeaderFlyweight layout.
void write_default_header(
    concurrent::UnsafeBuffer& buffer,
    i32 term_offset,
    i32 frame_length,
    i32 session_id,
    i32 stream_id,
    i32 term_id);

} // namespace caeron::logbuffer
