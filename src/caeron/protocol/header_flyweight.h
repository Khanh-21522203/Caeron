#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::protocol {

/// Base protocol header (8 bytes). All Aeron protocol frames share this layout.
///
///   0                   1                   2                   3
///   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
///  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///  |                        Frame Length                           |
///  +---------------+---------------+-------------------------------+
///  |  Version      |     Flags     |             Type              |
///  +---------------+---------------+-------------------------------+
class HeaderFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 8;

    // Frame type constants
    static constexpr u16 HDR_TYPE_PAD         = 0x00;
    static constexpr u16 HDR_TYPE_DATA        = 0x01;
    static constexpr u16 HDR_TYPE_NAK         = 0x02;
    static constexpr u16 HDR_TYPE_SM          = 0x03;
    static constexpr u16 HDR_TYPE_ERR         = 0x04;
    static constexpr u16 HDR_TYPE_SETUP       = 0x05;
    static constexpr u16 HDR_TYPE_RTTM        = 0x06;
    static constexpr u16 HDR_TYPE_RES         = 0x07;
    static constexpr u16 HDR_TYPE_ATS_DATA    = 0x08;
    static constexpr u16 HDR_TYPE_ATS_SM      = 0x09;
    static constexpr u16 HDR_TYPE_ATS_SETUP   = 0x0A;
    static constexpr u16 HDR_TYPE_RSP_SETUP   = 0x0B;
    static constexpr u16 HDR_TYPE_EXT         = 0xFFFF;

    static constexpr u8 CURRENT_VERSION = 0x0;

    explicit HeaderFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    [[nodiscard]] i32 frame_length() const noexcept
    {
        return buffer_.get_i32(offset_ + FRAME_LENGTH_FIELD_OFFSET);
    }

    HeaderFlyweight& set_frame_length(i32 length) noexcept
    {
        buffer_.put_i32(offset_ + FRAME_LENGTH_FIELD_OFFSET, length);
        return *this;
    }

    [[nodiscard]] u8 version() const noexcept
    {
        return buffer_.get_u8(offset_ + VERSION_FIELD_OFFSET);
    }

    HeaderFlyweight& set_version(u8 v) noexcept
    {
        buffer_.put_u8(offset_ + VERSION_FIELD_OFFSET, v);
        return *this;
    }

    [[nodiscard]] u8 flags() const noexcept
    {
        return buffer_.get_u8(offset_ + FLAGS_FIELD_OFFSET);
    }

    HeaderFlyweight& set_flags(u8 f) noexcept
    {
        buffer_.put_u8(offset_ + FLAGS_FIELD_OFFSET, f);
        return *this;
    }

    [[nodiscard]] u16 type() const noexcept
    {
        return buffer_.get_u16(offset_ + TYPE_FIELD_OFFSET);
    }

    HeaderFlyweight& set_type(u16 t) noexcept
    {
        buffer_.put_u16(offset_ + TYPE_FIELD_OFFSET, t);
        return *this;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    static constexpr i32 FRAME_LENGTH_FIELD_OFFSET = 0;
    static constexpr i32 VERSION_FIELD_OFFSET = 4;
    static constexpr i32 FLAGS_FIELD_OFFSET = 5;
    static constexpr i32 TYPE_FIELD_OFFSET = 6;

    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
