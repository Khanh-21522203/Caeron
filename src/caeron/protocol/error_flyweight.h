#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <string_view>

namespace caeron::protocol {

/// Error frame (variable length). Reports errors between driver and clients.
class ErrorFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 40;
    static constexpr i32 MAX_ERROR_MESSAGE_LENGTH = 1023;
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

    // Error-specific fields
    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    ErrorFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + 8, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + 12); }
    ErrorFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + 12, v); return *this; }

    [[nodiscard]] i32 error_code() const noexcept { return buffer_.get_i32(offset_ + 32); }
    ErrorFlyweight& set_error_code(i32 v) noexcept { buffer_.put_i32(offset_ + 32, v); return *this; }

    [[nodiscard]] i32 error_message_length() const noexcept { return buffer_.get_i32(offset_ + 36); }
    ErrorFlyweight& set_error_message_length(i32 v) noexcept { buffer_.put_i32(offset_ + 36, v); return *this; }

    void get_error_message(char* dst, i32 max_len) const noexcept
    {
        i32 len = error_message_length();
        if (len > max_len) len = max_len;
        buffer_.get_bytes(offset_ + HEADER_LENGTH, dst, len);
    }

    void set_error_message(const char* src, i32 len) noexcept
    {
        set_error_message_length(len);
        buffer_.put_bytes(offset_ + HEADER_LENGTH, src, len);
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
