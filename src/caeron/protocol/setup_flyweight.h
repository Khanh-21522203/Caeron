#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::protocol {

/// SETUP frame (40 bytes). Sent by publishers to initialize a stream.
class SetupFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 40;

    static constexpr u8 SEND_RESPONSE_SETUP_FLAG = 0x80;
    static constexpr u8 GROUP_FLAG = 0x40;

    explicit SetupFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Common header
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    SetupFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    SetupFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    SetupFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    SetupFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // SETUP-specific fields
    [[nodiscard]] i32 term_offset() const noexcept { return buffer_.get_i32(offset_ + 8); }
    SetupFlyweight& set_term_offset(i32 v) noexcept { buffer_.put_i32(offset_ + 8, v); return *this; }

    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + 12); }
    SetupFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + 12, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + 16); }
    SetupFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + 16, v); return *this; }

    [[nodiscard]] i32 initial_term_id() const noexcept { return buffer_.get_i32(offset_ + 20); }
    SetupFlyweight& set_initial_term_id(i32 v) noexcept { buffer_.put_i32(offset_ + 20, v); return *this; }

    [[nodiscard]] i32 active_term_id() const noexcept { return buffer_.get_i32(offset_ + 24); }
    SetupFlyweight& set_active_term_id(i32 v) noexcept { buffer_.put_i32(offset_ + 24, v); return *this; }

    [[nodiscard]] i32 term_length() const noexcept { return buffer_.get_i32(offset_ + 28); }
    SetupFlyweight& set_term_length(i32 v) noexcept { buffer_.put_i32(offset_ + 28, v); return *this; }

    [[nodiscard]] i32 mtu_length() const noexcept { return buffer_.get_i32(offset_ + 32); }
    SetupFlyweight& set_mtu_length(i32 v) noexcept { buffer_.put_i32(offset_ + 32, v); return *this; }

    [[nodiscard]] i32 ttl() const noexcept { return buffer_.get_i32(offset_ + 36); }
    SetupFlyweight& set_ttl(i32 v) noexcept { buffer_.put_i32(offset_ + 36, v); return *this; }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
