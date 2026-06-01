#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Layout (matches Java Aeron TerminateDriverFlyweight):
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i32  token_buffer_length
///   [20] u8[] token_buffer (variable)
class TerminateDriverFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET         = 0;
    static constexpr i32 CORRELATION_ID_OFFSET    = 8;
    static constexpr i32 TOKEN_LENGTH_OFFSET      = 16;
    static constexpr i32 TOKEN_OFFSET             = 20;

    explicit TerminateDriverFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i32 token_buffer_length() const noexcept { return buffer_.get_i32(offset_ + TOKEN_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_token_buffer() for bounds-checked writes.
    void set_token_buffer_length(i32 length) noexcept { buffer_.put_i32(offset_ + TOKEN_LENGTH_OFFSET, length); }

    /// Returns a pointer to the start of the token bytes. NOT null-terminated.
    [[nodiscard]] const char* token_buffer() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < TOKEN_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + TOKEN_OFFSET);
    }

    /// Copy token buffer bytes into the flyweight.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_token_buffer(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < TOKEN_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - TOKEN_OFFSET) return;
        set_token_buffer_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(offset_ + TOKEN_OFFSET, data, length);
    }

    /// Total byte length given the current token_buffer_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 len = token_buffer_length();
        if (len < 0) return -1;
        const i64 result = static_cast<i64>(TOKEN_OFFSET) + len;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for a given token length.
    [[nodiscard]] static i32 compute_length(i32 token_length) noexcept
    {
        if (token_length < 0) return -1;
        const i64 result = static_cast<i64>(TOKEN_OFFSET) + token_length;
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
