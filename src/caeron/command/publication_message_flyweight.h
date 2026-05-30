#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout (extends CorrelatedMessageFlyweight):
///   [ 0] i64  correlation_id
///   [ 8] i32  client_id
///   [12] i32  channel_length
///   [16] u8[] channel (variable)
///   [16+channel_length] i32 stream_id
class PublicationMessageFlyweight {
public:
    static constexpr i32 CHANNEL_LENGTH_OFFSET = 12;
    static constexpr i32 CHANNEL_OFFSET = 16;

    explicit PublicationMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 client_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    void set_client_id(i32 value) noexcept { buffer_.put_i32(offset_ + 8, value); }

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

    /// Total byte length of this flyweight given the current channel_length().
    [[nodiscard]] i32 length() const noexcept
    {
        return CHANNEL_OFFSET + channel_length() + 4; // 4 for stream_id
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
