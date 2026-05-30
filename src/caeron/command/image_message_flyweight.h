#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i32  stream_id
///   [12] i64  session_id
///   [20] i32  subscriber_position_count
///   [24] i32  position_indicator_ids_length (number of i64 entries)
///   [28] i64[] position_indicator_ids (variable: count * 8 bytes)
class ImageMessageFlyweight {
public:
    static constexpr i32 STREAM_ID_OFFSET                   = 8;
    static constexpr i32 SESSION_ID_OFFSET                  = 12;
    static constexpr i32 SUBSCRIBER_POSITION_COUNT_OFFSET   = 20;
    static constexpr i32 POSITION_INDICATOR_COUNT_OFFSET    = 24;
    static constexpr i32 POSITION_INDICATOR_IDS_OFFSET      = 28;

    explicit ImageMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    [[nodiscard]] i64 session_id() const noexcept { return buffer_.get_i64(offset_ + SESSION_ID_OFFSET); }
    void set_session_id(i64 value) noexcept { buffer_.put_i64(offset_ + SESSION_ID_OFFSET, value); }

    [[nodiscard]] i32 subscriber_position_count() const noexcept { return buffer_.get_i32(offset_ + SUBSCRIBER_POSITION_COUNT_OFFSET); }
    void set_subscriber_position_count(i32 value) noexcept { buffer_.put_i32(offset_ + SUBSCRIBER_POSITION_COUNT_OFFSET, value); }

    [[nodiscard]] i32 position_indicator_ids_count() const noexcept { return buffer_.get_i32(offset_ + POSITION_INDICATOR_COUNT_OFFSET); }
    void set_position_indicator_ids_count(i32 count) noexcept { buffer_.put_i32(offset_ + POSITION_INDICATOR_COUNT_OFFSET, count); }

    [[nodiscard]] i64 position_indicator_id(i32 index) const noexcept
    {
        return buffer_.get_i64(offset_ + POSITION_INDICATOR_IDS_OFFSET + index * 8);
    }

    void set_position_indicator_id(i32 index, i64 value) noexcept
    {
        buffer_.put_i64(offset_ + POSITION_INDICATOR_IDS_OFFSET + index * 8, value);
    }

    /// Total byte length given the current position_indicator_ids_count().
    [[nodiscard]] i32 length() const noexcept
    {
        return POSITION_INDICATOR_IDS_OFFSET + position_indicator_ids_count() * 8;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
