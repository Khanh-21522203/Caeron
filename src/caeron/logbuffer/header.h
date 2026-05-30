#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::logbuffer {

/// A lightweight header view that pairs a term buffer reference with the
/// cached metadata fields (session, stream, term, and absolute position)
/// that describe the frame at the current offset.
struct Header
{
    concurrent::UnsafeBuffer& buffer;
    i32 offset;
    i32 session_id;
    i32 stream_id;
    i32 term_id;
    i64 position;
};

} // namespace caeron::logbuffer
