#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <stdexcept>

namespace caeron::protocol {

/// Status Message frame (36 bytes, optional 44 bytes with group tag).
/// Sent by receivers to report consumption position and window.
class StatusMessageFlyweight
{
public:
    static constexpr i32 HEADER_LENGTH = 36;
    static constexpr i32 HEADER_LENGTH_WITH_GROUP_TAG = 44;

    static constexpr u8 SEND_SETUP_FLAG     = 0x80;
    static constexpr u8 END_OF_STREAM_FLAG  = 0x40;
    static constexpr u8 HAS_GROUP_ID_FLAG   = 0x08;

    /// Offset for Application Specific Feedback (ASF) / group tag.
    static constexpr i32 APP_SPECIFIC_FEEDBACK_FIELD_OFFSET = 36;
    static constexpr i32 GROUP_TAG_FIELD_OFFSET = APP_SPECIFIC_FEEDBACK_FIELD_OFFSET;

    explicit StatusMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Common header
    [[nodiscard]] i32 frame_length() const noexcept { return buffer_.get_i32(offset_); }
    StatusMessageFlyweight& set_frame_length(i32 v) noexcept { buffer_.put_i32(offset_, v); return *this; }

    [[nodiscard]] u8 version() const noexcept { return buffer_.get_u8(offset_ + 4); }
    StatusMessageFlyweight& set_version(u8 v) noexcept { buffer_.put_u8(offset_ + 4, v); return *this; }

    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + 5); }
    StatusMessageFlyweight& set_flags(u8 v) noexcept { buffer_.put_u8(offset_ + 5, v); return *this; }

    [[nodiscard]] u16 type() const noexcept { return buffer_.get_u16(offset_ + 6); }
    StatusMessageFlyweight& set_type(u16 v) noexcept { buffer_.put_u16(offset_ + 6, v); return *this; }

    // SM-specific fields
    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    StatusMessageFlyweight& set_session_id(i32 v) noexcept { buffer_.put_i32(offset_ + 8, v); return *this; }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + 12); }
    StatusMessageFlyweight& set_stream_id(i32 v) noexcept { buffer_.put_i32(offset_ + 12, v); return *this; }

    [[nodiscard]] i32 consumption_term_id() const noexcept { return buffer_.get_i32(offset_ + 16); }
    StatusMessageFlyweight& set_consumption_term_id(i32 v) noexcept { buffer_.put_i32(offset_ + 16, v); return *this; }

    [[nodiscard]] i32 consumption_term_offset() const noexcept { return buffer_.get_i32(offset_ + 20); }
    StatusMessageFlyweight& set_consumption_term_offset(i32 v) noexcept { buffer_.put_i32(offset_ + 20, v); return *this; }

    [[nodiscard]] i32 receiver_window() const noexcept { return buffer_.get_i32(offset_ + 24); }
    StatusMessageFlyweight& set_receiver_window(i32 v) noexcept { buffer_.put_i32(offset_ + 24, v); return *this; }

    [[nodiscard]] i64 receiver_id() const noexcept { return buffer_.get_i64(offset_ + 28); }
    StatusMessageFlyweight& set_receiver_id(i64 v) noexcept { buffer_.put_i64(offset_ + 28, v); return *this; }

    /// Length of Application Specific Feedback (or group tag) in bytes.
    [[nodiscard]] i32 asf_length() const noexcept { return frame_length() - HEADER_LENGTH; }

    /// Optional group tag at offset 36 (present when frame_length == 44).
    /// Returns 0 if no group tag is present (frame_length == 36).
    /// Throws if ASF length is nonzero but not exactly 8 bytes (malformed frame).
    [[nodiscard]] i64 group_tag() const
    {
        const i32 len = frame_length();
        if (len == HEADER_LENGTH_WITH_GROUP_TAG)
        {
            return buffer_.get_i64(offset_ + GROUP_TAG_FIELD_OFFSET);
        }
        if (len > HEADER_LENGTH)
        {
            throw std::runtime_error(
                "SM has application specific feedback (" +
                std::to_string(len - HEADER_LENGTH) + ") but not a valid group tag (expected 0 or 8)");
        }
        return 0;
    }

    /// Set group tag — also sets frame_length to HEADER_LENGTH_WITH_GROUP_TAG.
    StatusMessageFlyweight& set_group_tag(i64 v) noexcept
    {
        set_frame_length(HEADER_LENGTH_WITH_GROUP_TAG);
        buffer_.put_i64(offset_ + GROUP_TAG_FIELD_OFFSET, v);
        return *this;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
