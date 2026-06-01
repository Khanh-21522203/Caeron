#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

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
    /// RAW FIELD SETTER -- no validation. Prefer set_error_message() for bounds-checked writes.
    void set_error_message_length(i32 length) noexcept { buffer_.put_i32(offset_ + ERROR_MESSAGE_LENGTH_OFFSET, length); }

    /// Returns a pointer to the error message bytes. NOT null-terminated.
    [[nodiscard]] const char* error_message() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ERROR_MESSAGE_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + ERROR_MESSAGE_OFFSET);
    }

    /// Copy error message bytes into the buffer.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_error_message(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ERROR_MESSAGE_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - ERROR_MESSAGE_OFFSET) return;
        set_error_message_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(offset_ + ERROR_MESSAGE_OFFSET, data, length);
    }

    /// Total byte length given the current error_message_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 len = error_message_length();
        if (len < 0) return -1;
        const i64 result = static_cast<i64>(ERROR_MESSAGE_OFFSET) + len;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for a given error message string length.
    [[nodiscard]] static i32 compute_length(i32 error_message_length) noexcept
    {
        if (error_message_length < 0) return -1;
        const i64 result = static_cast<i64>(ERROR_MESSAGE_OFFSET) + error_message_length;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
