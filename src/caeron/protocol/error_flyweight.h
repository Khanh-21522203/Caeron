#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <stdexcept>
#include <string>

namespace caeron::protocol {

/// Error frame (variable length). Reports errors between driver and clients.
///
/// Layout matching Java Aeron 1.47.0+ ErrorFlyweight.java:
/// <pre>
///    0                   1                   2                   3
///    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// 0  |R|                 Frame Length (varies)                       |
///    +---------------+---------------+-------------------------------+
/// 4  |   Version     |     Flags     |         Type (=0x04)          |
///    +---------------+---------------+-------------------------------+
/// 8  |                          Session ID                           |
///    +---------------------------------------------------------------+
/// 12 |                           Stream ID                           |
///    +---------------------------------------------------------------+
/// 16 |                          Receiver ID                          |
///    |                                                               |
///    +---------------------------------------------------------------+
/// 24 |                           Group Tag                           |
///    |                                                               |
///    +---------------------------------------------------------------+
/// 32 |                          Error Code                           |
///    +---------------------------------------------------------------+
/// 36 |                     Error String Length                       |
///    +---------------------------------------------------------------+
/// 40 |                         Error String                        ...
///    +---------------------------------------------------------------+
/// </pre>
class ErrorFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 40;

    static constexpr u8 HAS_GROUP_ID_FLAG = 0x08;

    // Field offsets
    static constexpr i32 SESSION_ID_FIELD_OFFSET = 8;
    static constexpr i32 STREAM_ID_FIELD_OFFSET = 12;
    static constexpr i32 RECEIVER_ID_FIELD_OFFSET = 16;
    static constexpr i32 GROUP_TAG_FIELD_OFFSET = 24;
    static constexpr i32 ERROR_CODE_FIELD_OFFSET = 32;
    static constexpr i32 ERROR_STRING_FIELD_OFFSET = 36;

    /// Maximum error message length.
    static constexpr i32 MAX_ERROR_MESSAGE_LENGTH = 1023;
    /// Maximum error frame length.
    static constexpr i32 MAX_ERROR_FRAME_LENGTH = HEADER_LENGTH + MAX_ERROR_MESSAGE_LENGTH;

    explicit ErrorFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Common header
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    ErrorFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    ErrorFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    ErrorFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    ErrorFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // Session ID
    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + SESSION_ID_FIELD_OFFSET); }
    ErrorFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + SESSION_ID_FIELD_OFFSET, v); return *this; }

    // Stream ID
    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_FIELD_OFFSET); }
    ErrorFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + STREAM_ID_FIELD_OFFSET, v); return *this; }

    // Receiver ID
    [[nodiscard]] i64 receiver_id() const noexcept { return buffer_.get_i64(offset_ + RECEIVER_ID_FIELD_OFFSET); }
    ErrorFlyweight& set_receiver_id(i64 v) noexcept { buffer_.put_i64(offset_ + RECEIVER_ID_FIELD_OFFSET, v); return *this; }

    // Group Tag
    [[nodiscard]] i64 group_tag() const noexcept { return buffer_.get_i64(offset_ + GROUP_TAG_FIELD_OFFSET); }
    ErrorFlyweight& set_group_tag(i64 v) noexcept
    {
        buffer_.put_i64(offset_ + GROUP_TAG_FIELD_OFFSET, v);
        set_flags(flags() | HAS_GROUP_ID_FLAG);
        return *this;
    }
    [[nodiscard]] bool has_group_tag() const noexcept { return (flags() & HAS_GROUP_ID_FLAG) != 0; }

    // Error Code
    [[nodiscard]] i32 error_code() const noexcept { return buffer_.get_i32(offset_ + ERROR_CODE_FIELD_OFFSET); }
    ErrorFlyweight& set_error_code(i32 v) noexcept { buffer_.put_i32(offset_ + ERROR_CODE_FIELD_OFFSET, v); return *this; }

    // Error String (length-prefixed)
    [[nodiscard]] i32 error_message_length() const noexcept { return buffer_.get_i32(offset_ + ERROR_STRING_FIELD_OFFSET); }

    [[nodiscard]] std::string error_message() const
    {
        const i32 len = error_message_length();
        if (len <= 0) return {};
        std::string result(static_cast<size_t>(len), '\0');
        buffer_.get_bytes(offset_ + HEADER_LENGTH, result.data(), len);
        return result;
    }

    void set_error_message(const std::string& msg)
    {
        const auto len = static_cast<i32>(msg.size());
        if (len > MAX_ERROR_MESSAGE_LENGTH)
        {
            throw std::out_of_range("error message length exceeds MAX_ERROR_MESSAGE_LENGTH");
        }
        buffer_.put_i32(offset_ + ERROR_STRING_FIELD_OFFSET, len);
        buffer_.put_bytes(offset_ + HEADER_LENGTH, msg.data(), len);
        set_frame_length(HEADER_LENGTH + len);
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
