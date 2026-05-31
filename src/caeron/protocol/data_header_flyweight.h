#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::protocol {

/// DATA frame header (32 bytes). Sent by publishers to transmit data.
///
///   0                   1                   2                   3
///   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
///  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///  |                        Frame Length                           |
///  +---------------+---------------+-------------------------------+
///  |   Version     |     Flags     |         Type (=0x01)          |
///  +---------------+---------------+-------------------------------+
///  |                         Term Offset                           |
///  +---------------------------------------------------------------+
///  |                         Session ID                            |
///  +---------------------------------------------------------------+
///  |                          Stream ID                            |
///  +---------------------------------------------------------------+
///  |                          Term ID                              |
///  +---------------------------------------------------------------+
///  |                      Reserved Value (8 bytes)                 |
///  +---------------------------------------------------------------+
///  |                        Data Payload                          ...
///  +---------------------------------------------------------------+
class DataHeaderFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 32;

    // Flag constants
    static constexpr u8 BEGIN_FLAG = 0x80;
    static constexpr u8 END_FLAG   = 0x40;
    static constexpr u8 EOS_FLAG   = 0x20;
    static constexpr u8 REVOKED_FLAG = 0x10;
    static constexpr u8 UNFRAGMENTED = BEGIN_FLAG | END_FLAG;

    // Combined flag constants
    static constexpr u8 BEGIN_AND_END_FLAGS = BEGIN_FLAG | END_FLAG;
    static constexpr u8 BEGIN_END_AND_EOS_FLAGS = BEGIN_FLAG | END_FLAG | EOS_FLAG;
    static constexpr u8 BEGIN_END_EOS_AND_REVOKED_FLAGS = BEGIN_FLAG | END_FLAG | EOS_FLAG | REVOKED_FLAG;

    explicit DataHeaderFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Frame header fields (inherited from HeaderFlyweight layout)
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    DataHeaderFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    DataHeaderFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    DataHeaderFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    DataHeaderFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // DATA-specific fields
    [[nodiscard]] i32 term_offset() const noexcept { return buffer_.get_i32(offset_ + TERM_OFFSET_FIELD_OFFSET); }
    DataHeaderFlyweight& set_term_offset(i32 v) noexcept { buffer_.put_i32(offset_ + TERM_OFFSET_FIELD_OFFSET, v); return *this; }

    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + SESSION_ID_FIELD_OFFSET); }
    DataHeaderFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + SESSION_ID_FIELD_OFFSET, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_FIELD_OFFSET); }
    DataHeaderFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + STREAM_ID_FIELD_OFFSET, v); return *this; }

    [[nodiscard]] i32 term_id() const noexcept { return buffer_.get_i32(offset_ + TERM_ID_FIELD_OFFSET); }
    DataHeaderFlyweight& set_term_id(i32 v) noexcept { buffer_.put_i32(offset_ + TERM_ID_FIELD_OFFSET, v); return *this; }

    [[nodiscard]] i64 reserved_value() const noexcept { return buffer_.get_i64(offset_ + RESERVED_VALUE_FIELD_OFFSET); }
    DataHeaderFlyweight& set_reserved_value(i64 v) noexcept { buffer_.put_i64(offset_ + RESERVED_VALUE_FIELD_OFFSET, v); return *this; }

    /// Check if the EOS flag is set in the given buffer at the specified offset.
    [[nodiscard]] static bool is_end_of_stream(const concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
    {
        return (buffer.get_u8(offset + FLAGS_FIELD_OFFSET) & EOS_FLAG) != 0;
    }

    /// Check if the REVOKED flag is set in the given buffer at the specified offset.
    [[nodiscard]] static bool is_revoked(const concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
    {
        return (buffer.get_u8(offset + FLAGS_FIELD_OFFSET) & REVOKED_FLAG) != 0;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    static constexpr i32 FLAGS_FIELD_OFFSET = 5;
    static constexpr i32 TERM_OFFSET_FIELD_OFFSET = 8;
    static constexpr i32 SESSION_ID_FIELD_OFFSET = 12;
    static constexpr i32 STREAM_ID_FIELD_OFFSET = 16;
    static constexpr i32 TERM_ID_FIELD_OFFSET = 20;
    static constexpr i32 RESERVED_VALUE_FIELD_OFFSET = 24;

    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
