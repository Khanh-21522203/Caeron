#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::protocol {

/// NAK frame (28 bytes). Sent by receivers to request retransmission of lost data.
class NakFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 28;

    explicit NakFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Common header
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    NakFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    NakFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    NakFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    NakFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // NAK-specific fields
    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    NakFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + 8, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + 12); }
    NakFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + 12, v); return *this; }

    [[nodiscard]] i32 term_id() const noexcept { return buffer_.get_i32(offset_ + 16); }
    NakFlyweight& set_term_id(i32 v) noexcept { buffer_.put_i32(offset_ + 16, v); return *this; }

    [[nodiscard]] i32 term_offset() const noexcept { return buffer_.get_i32(offset_ + 20); }
    NakFlyweight& set_term_offset(i32 v) noexcept { buffer_.put_i32(offset_ + 20, v); return *this; }

    [[nodiscard]] i32 length() const noexcept { return buffer_.get_i32(offset_ + 24); }
    NakFlyweight& set_length(i32 v) noexcept { buffer_.put_i32(offset_ + 24, v); return *this; }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
