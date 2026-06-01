#pragma once

#include "caeron/common/bit_util.h"
#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Layout (matches Java Aeron ImageBuffersReadyFlyweight):
///   [ 0] i64  correlation_id
///   [ 8] i32  session_id
///   [12] i32  stream_id
///   [16] i64  subscription_registration_id
///   [24] i32  subscriber_position_id
///   [28] i32  log_file_name_length
///   [32] u8[] log_file_name (variable)
///   [32+log_len] i32  source_identity_length
///   [+4]         u8[] source_identity (variable)
inline constexpr i32 IMAGE_BUFFERS_READY_MINIMUM_LENGTH = 32;

class ImageBuffersReadyFlyweight {
public:
    static constexpr i32 CORRELATION_ID_OFFSET                  = 0;
    static constexpr i32 SESSION_ID_OFFSET                      = 8;
    static constexpr i32 STREAM_ID_OFFSET                       = 12;
    static constexpr i32 SUBSCRIPTION_REGISTRATION_ID_OFFSET    = 16;
    static constexpr i32 SUBSCRIBER_POSITION_ID_OFFSET          = 24;
    static constexpr i32 LOG_FILE_NAME_LENGTH_OFFSET            = 28;
    static constexpr i32 LOG_FILE_NAME_OFFSET                   = 32;

    explicit ImageBuffersReadyFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + SESSION_ID_OFFSET); }
    void set_session_id(i32 value) noexcept { buffer_.put_i32(offset_ + SESSION_ID_OFFSET, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    [[nodiscard]] i64 subscription_registration_id() const noexcept { return buffer_.get_i64(offset_ + SUBSCRIPTION_REGISTRATION_ID_OFFSET); }
    void set_subscription_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + SUBSCRIPTION_REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i32 subscriber_position_id() const noexcept { return buffer_.get_i32(offset_ + SUBSCRIBER_POSITION_ID_OFFSET); }
    void set_subscriber_position_id(i32 value) noexcept { buffer_.put_i32(offset_ + SUBSCRIBER_POSITION_ID_OFFSET, value); }

    [[nodiscard]] i32 log_file_name_length() const noexcept { return buffer_.get_i32(offset_ + LOG_FILE_NAME_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_log_file_name() for bounds-checked writes.
    void set_log_file_name_length(i32 length) noexcept { buffer_.put_i32(offset_ + LOG_FILE_NAME_LENGTH_OFFSET, length); }

    /// Returns a pointer to the log file name bytes. NOT null-terminated.
    [[nodiscard]] const char* log_file_name() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(
            buffer_.data() + offset_ + LOG_FILE_NAME_OFFSET);
    }

    /// Copy log file name bytes into the buffer.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_log_file_name(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - LOG_FILE_NAME_OFFSET) return;
        set_log_file_name_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(offset_ + LOG_FILE_NAME_OFFSET, data, length);
    }

    [[nodiscard]] i32 source_identity_length() const noexcept
    {
        if (offset_ < 0) return -1;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return -1;
        const i32 log_len = log_file_name_length();
        if (log_len < 0) return -1;
        if (log_len > buffer_.capacity() - offset_ - LOG_FILE_NAME_OFFSET - SIZE_OF_INT) return -1;
        return buffer_.get_i32(offset_ + LOG_FILE_NAME_OFFSET + log_len);
    }

    /// RAW FIELD SETTER -- no validation. Prefer set_source_identity() for bounds-checked writes.
    void set_source_identity_length(i32 length) noexcept
    {
        if (offset_ < 0) return;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return;
        const i32 log_len = log_file_name_length();
        if (log_len < 0) return;
        if (log_len > buffer_.capacity() - offset_ - LOG_FILE_NAME_OFFSET - SIZE_OF_INT) return;
        buffer_.put_i32(offset_ + LOG_FILE_NAME_OFFSET + log_len, length);
    }

    /// Returns a pointer to the source identity bytes. NOT null-terminated.
    [[nodiscard]] const char* source_identity() const noexcept
    {
        if (offset_ < 0) return nullptr;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return nullptr;
        const i32 log_len = log_file_name_length();
        if (log_len < 0) return nullptr;
        if (log_len > buffer_.capacity() - offset_ - LOG_FILE_NAME_OFFSET - SIZE_OF_INT) return nullptr;
        return reinterpret_cast<const char*>(
            buffer_.data() + offset_ + LOG_FILE_NAME_OFFSET + log_len + SIZE_OF_INT);
    }

    /// Copy source identity bytes into the buffer.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_source_identity(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0) return;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return;
        const i32 log_len = log_file_name_length();
        if (log_len < 0) return;
        if (log_len > buffer_.capacity() - offset_ - LOG_FILE_NAME_OFFSET - SIZE_OF_INT) return;
        const i32 label_data_offset = offset_ + LOG_FILE_NAME_OFFSET + log_len + SIZE_OF_INT;
        if (length > buffer_.capacity() - label_data_offset) return;
        set_source_identity_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(label_data_offset, data, length);
    }

    /// Total byte length of this flyweight given the current field lengths.
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 log_len = log_file_name_length();
        if (log_len < 0) return -1;
        const i32 src_len = source_identity_length();
        if (src_len < 0) return -1;
        const i64 result = static_cast<i64>(LOG_FILE_NAME_OFFSET) + log_len + 4 + src_len;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for given string lengths.
    [[nodiscard]] static i32 compute_length(i32 log_len, i32 source_identity_len) noexcept
    {
        if (log_len < 0 || source_identity_len < 0) return -1;
        const i64 result = static_cast<i64>(LOG_FILE_NAME_OFFSET) + log_len + 4 + source_identity_len;
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
