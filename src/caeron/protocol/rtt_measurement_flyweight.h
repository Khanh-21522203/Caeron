#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::protocol {

/// RTT Measurement frame (40 bytes). Used for round-trip time measurement.
class RttMeasurementFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 40;

    static constexpr u8 REPLY_FLAG = 0x80;

    explicit RttMeasurementFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Common header
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    RttMeasurementFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    RttMeasurementFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    RttMeasurementFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    RttMeasurementFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // RTT-specific fields
    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    RttMeasurementFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + 8, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + 12); }
    RttMeasurementFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + 12, v); return *this; }

    [[nodiscard]] i64 echo_timestamp() const noexcept { return buffer_.get_i64(offset_ + 16); }
    RttMeasurementFlyweight& set_echo_timestamp(i64 v) noexcept { buffer_.put_i64(offset_ + 16, v); return *this; }

    [[nodiscard]] i64 reception_delta() const noexcept { return buffer_.get_i64(offset_ + 24); }
    RttMeasurementFlyweight& set_reception_delta(i64 v) noexcept { buffer_.put_i64(offset_ + 24, v); return *this; }

    [[nodiscard]] i64 receiver_id() const noexcept { return buffer_.get_i64(offset_ + 32); }
    RttMeasurementFlyweight& set_receiver_id(i64 v) noexcept { buffer_.put_i64(offset_ + 32, v); return *this; }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
