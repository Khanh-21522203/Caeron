#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Layout (matches Java Aeron ImageMessageFlyweight):
///   [ 0] i64  correlation_id
///   [ 8] i64  subscription_registration_id
///   [16] i32  stream_id
///   [20] u8[] channel (variable, may be empty)
inline constexpr i32 IMAGE_MESSAGE_MINIMUM_LENGTH = 20;

class ImageMessageFlyweight {
public:
    static constexpr i32 CORRELATION_ID_OFFSET                  = 0;
    static constexpr i32 SUBSCRIPTION_REGISTRATION_ID_OFFSET    = 8;
    static constexpr i32 STREAM_ID_OFFSET                       = 16;
    static constexpr i32 CHANNEL_OFFSET                         = 20;

    explicit ImageMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i64 subscription_registration_id() const noexcept { return buffer_.get_i64(offset_ + SUBSCRIPTION_REGISTRATION_ID_OFFSET); }
    void set_subscription_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + SUBSCRIPTION_REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    /// Returns a pointer to the start of the channel bytes. NOT null-terminated.
    [[nodiscard]] const char* channel() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < CHANNEL_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + CHANNEL_OFFSET);
    }

    /// Copy channel bytes into the buffer starting at CHANNEL_OFFSET.
    ///
    /// Contract:
    ///   - Negative length is a no-op (caller error is silently ignored).
    ///   - If data is nullptr or length is zero, the method returns immediately
    ///     without writing (clamp-to-zero). This avoids dereferencing a null
    ///     pointer or issuing a zero-length memcpy, both of which are
    ///     undefined behavior on some platforms.
    void set_channel(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (data == nullptr || length == 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < CHANNEL_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - CHANNEL_OFFSET) return;
        buffer_.put_bytes(offset_ + CHANNEL_OFFSET, data, length);
    }

    /// Total byte length for a given channel length.
    [[nodiscard]] static i32 compute_length(i32 channel_length) noexcept
    {
        if (channel_length < 0) return -1;
        const i64 result = static_cast<i64>(CHANNEL_OFFSET) + channel_length;
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
