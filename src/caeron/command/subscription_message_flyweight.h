#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i32  channel_length
///   [ 4] u8[] channel (variable)
///   [ 4+channel_length] i32 stream_id
///   [ 8+channel_length] i64 registration_correlation_id
class SubscriptionMessageFlyweight {
public:
    static constexpr i32 CHANNEL_LENGTH_OFFSET = 0;
    static constexpr i32 CHANNEL_OFFSET = 4;

    explicit SubscriptionMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i32 channel_length() const noexcept { return buffer_.get_i32(offset_ + CHANNEL_LENGTH_OFFSET); }
    void set_channel_length(i32 length) noexcept { buffer_.put_i32(offset_ + CHANNEL_LENGTH_OFFSET, length); }

    [[nodiscard]] const char* channel() const noexcept
    {
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + CHANNEL_OFFSET);
    }

    void set_channel(const char* data, i32 length) noexcept
    {
        set_channel_length(length);
        buffer_.put_bytes(offset_ + CHANNEL_OFFSET, data, length);
    }

    [[nodiscard]] i32 stream_id() const noexcept
    {
        return buffer_.get_i32(offset_ + CHANNEL_OFFSET + channel_length());
    }

    void set_stream_id(i32 value) noexcept
    {
        buffer_.put_i32(offset_ + CHANNEL_OFFSET + channel_length(), value);
    }

    [[nodiscard]] i64 registration_correlation_id() const noexcept
    {
        return buffer_.get_i64(offset_ + CHANNEL_OFFSET + channel_length() + 4);
    }

    void set_registration_correlation_id(i64 value) noexcept
    {
        buffer_.put_i64(offset_ + CHANNEL_OFFSET + channel_length() + 4, value);
    }

    /// Total byte length of this flyweight given the current channel_length().
    [[nodiscard]] i32 length() const noexcept
    {
        return CHANNEL_OFFSET + channel_length() + 4 + 8; // stream_id(4) + reg_corr_id(8)
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
