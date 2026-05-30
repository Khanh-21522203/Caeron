#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i64  registration_id
///   [16] i64  session_id
///   [24] i32  subscriber_position_count
///   [28] i32  position_indicator_ids_count
///   [32] i64[] position_indicator_ids (variable: count * 8 bytes)
///   [32+count*8] i32  log_file_name_length
///   [36+count*8] u8[] log_file_name (variable)
///   [36+count*8+log_len] i32  source_identity_length
///   [40+count*8+log_len] u8[] source_identity (variable)
class ImageBuffersReadyFlyweight {
public:
    static constexpr i32 REGISTRATION_ID_OFFSET            = 8;
    static constexpr i32 SESSION_ID_OFFSET                 = 16;
    static constexpr i32 SUBSCRIBER_POSITION_COUNT_OFFSET  = 24;
    static constexpr i32 POSITION_INDICATOR_COUNT_OFFSET   = 28;
    static constexpr i32 POSITION_INDICATOR_IDS_OFFSET     = 32;

    explicit ImageBuffersReadyFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + REGISTRATION_ID_OFFSET); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + REGISTRATION_ID_OFFSET, value); }

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

    /// Offset where log_file_name_length begins.
    [[nodiscard]] i32 log_file_name_length_offset() const noexcept
    {
        return offset_ + POSITION_INDICATOR_IDS_OFFSET + position_indicator_ids_count() * 8;
    }

    [[nodiscard]] i32 log_file_name_length() const noexcept
    {
        return buffer_.get_i32(log_file_name_length_offset());
    }

    void set_log_file_name_length(i32 length) noexcept
    {
        buffer_.put_i32(log_file_name_length_offset(), length);
    }

    [[nodiscard]] const char* log_file_name() const noexcept
    {
        return reinterpret_cast<const char*>(
            buffer_.data() + log_file_name_length_offset() + 4);
    }

    void set_log_file_name(const char* data, i32 length) noexcept
    {
        set_log_file_name_length(length);
        buffer_.put_bytes(log_file_name_length_offset() + 4, data, length);
    }

    /// Offset where source_identity_length begins.
    [[nodiscard]] i32 source_identity_length_offset() const noexcept
    {
        return log_file_name_length_offset() + 4 + log_file_name_length();
    }

    [[nodiscard]] i32 source_identity_length() const noexcept
    {
        return buffer_.get_i32(source_identity_length_offset());
    }

    void set_source_identity_length(i32 length) noexcept
    {
        buffer_.put_i32(source_identity_length_offset(), length);
    }

    [[nodiscard]] const char* source_identity() const noexcept
    {
        return reinterpret_cast<const char*>(
            buffer_.data() + source_identity_length_offset() + 4);
    }

    void set_source_identity(const char* data, i32 length) noexcept
    {
        set_source_identity_length(length);
        buffer_.put_bytes(source_identity_length_offset() + 4, data, length);
    }

    /// Total byte length of this flyweight given current variable-length fields.
    [[nodiscard]] i32 length() const noexcept
    {
        return source_identity_length_offset() + 4 + source_identity_length() - offset_;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
