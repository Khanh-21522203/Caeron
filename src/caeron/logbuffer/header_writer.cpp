#include "header_writer.h"

namespace caeron::logbuffer {

// DataHeaderFlyweight field offsets (matching protocol/data_header_flyweight.h)
static constexpr i32 FRAME_LENGTH_FIELD_OFFSET    = 0;
static constexpr i32 VERSION_FIELD_OFFSET          = 4;
static constexpr i32 FLAGS_FIELD_OFFSET             = 5;
static constexpr i32 TYPE_FIELD_OFFSET              = 6;
static constexpr i32 TERM_OFFSET_FIELD_OFFSET       = 8;
static constexpr i32 SESSION_ID_FIELD_OFFSET        = 12;
static constexpr i32 STREAM_ID_FIELD_OFFSET         = 16;
static constexpr i32 TERM_ID_FIELD_OFFSET           = 20;

static constexpr u8  CURRENT_VERSION = 0x0;
static constexpr u8  UNFRAGMENTED   = 0x80 | 0x40; // BEGIN | END
static constexpr u16 HDR_TYPE_DATA  = 0x01;

void write_default_header(
    concurrent::UnsafeBuffer& buffer,
    i32 term_offset,
    i32 frame_length,
    i32 session_id,
    i32 stream_id,
    i32 term_id)
{
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
