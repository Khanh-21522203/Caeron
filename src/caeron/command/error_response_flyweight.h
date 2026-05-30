#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  offending_correlation_id
///   [ 8] i32  error_code
///   [12] i32  error_message_length
///   [16] u8[] error_message (variable)
class ErrorResponseFlyweight {
public:
    static constexpr i32 OFFENDING_CORRELATION_ID_OFFSET = 0;
    static constexpr i32 ERROR_CODE_OFFSET               = 8;
    static constexpr i32 ERROR_MESSAGE_LENGTH_OFFSET     = 12;
    static constexpr i32 ERROR_MESSAGE_OFFSET            = 16;

    explicit ErrorResponseFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 offending_correlation_id() const noexcept { return buffer_.get_i64(offset_ + OFFENDING_CORRELATION_ID_OFFSET); }
    void set_offending_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + OFFENDING_CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i32 error_code() const noexcept { return buffer_.get_i32(offset_ + ERROR_CODE_OFFSET); }
    void set_error_code(i32 value) noexcept { buffer_.put_i32(offset_ + ERROR_CODE_OFFSET, value); }

    [[nodiscard]] i32 error_message_length() const noexcept { return buffer_.get_i32(offset_ + ERROR_MESSAGE_LENGTH_OFFSET); }
    void set_error_message_length(i32 length) noexcept { buffer_.put_i32(offset_ + ERROR_MESSAGE_LENGTH_OFFSET, length); }

    [[nodiscard]] const char* error_message() const noexcept
    {
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + ERROR_MESSAGE_OFFSET);
    }

    void set_error_message(const char* data, i32 length) noexcept
    {
        set_error_message_length(length);
        buffer_.put_bytes(offset_ + ERROR_MESSAGE_OFFSET, data, length);
    }

    /// Total byte length given the current error_message_length().
    [[nodiscard]] i32 length() const noexcept
    {
        return ERROR_MESSAGE_OFFSET + error_message_length();
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
