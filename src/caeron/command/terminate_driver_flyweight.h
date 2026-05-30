#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i32  client_id
///   [12] i32  token_buffer_length
///   [16] u8[] token_buffer (variable)
class TerminateDriverFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET         = 8;
    static constexpr i32 TOKEN_LENGTH_OFFSET      = 12;
    static constexpr i32 TOKEN_OFFSET             = 16;

    explicit TerminateDriverFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 client_id() const noexcept { return buffer_.get_i32(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i32 value) noexcept { buffer_.put_i32(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i32 token_buffer_length() const noexcept { return buffer_.get_i32(offset_ + TOKEN_LENGTH_OFFSET); }
    void set_token_buffer_length(i32 length) noexcept { buffer_.put_i32(offset_ + TOKEN_LENGTH_OFFSET, length); }

    [[nodiscard]] const char* token_buffer() const noexcept
    {
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + TOKEN_OFFSET);
    }

    void set_token_buffer(const char* data, i32 length) noexcept
    {
        set_token_buffer_length(length);
        buffer_.put_bytes(offset_ + TOKEN_OFFSET, data, length);
    }

    /// Total byte length given the current token_buffer_length().
    [[nodiscard]] i32 length() const noexcept
    {
        return TOKEN_OFFSET + token_buffer_length();
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
