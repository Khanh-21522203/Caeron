#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i64  registration_id
///   [16] i32  session_id
///   [20] i32  stream_id
///   [24] i32  pub_limit_counter_id
///   [28] i32  channel_status_counter_id
///   [32] i32  log_file_name_length
///   [36] u8[] log_file_name (variable)
class PublicationBuffersReadyFlyweight {
public:
    static constexpr i32 REGISTRATION_ID_OFFSET          = 8;
    static constexpr i32 SESSION_ID_OFFSET               = 16;
    static constexpr i32 STREAM_ID_OFFSET                = 20;
    static constexpr i32 PUB_LIMIT_COUNTER_ID_OFFSET     = 24;
    static constexpr i32 CHANNEL_STATUS_COUNTER_ID_OFFSET = 28;
    static constexpr i32 LOG_FILE_NAME_LENGTH_OFFSET     = 32;
    static constexpr i32 LOG_FILE_NAME_OFFSET            = 36;

    explicit PublicationBuffersReadyFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + REGISTRATION_ID_OFFSET); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + SESSION_ID_OFFSET); }
    void set_session_id(i32 value) noexcept { buffer_.put_i32(offset_ + SESSION_ID_OFFSET, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    [[nodiscard]] i32 pub_limit_counter_id() const noexcept { return buffer_.get_i32(offset_ + PUB_LIMIT_COUNTER_ID_OFFSET); }
    void set_pub_limit_counter_id(i32 value) noexcept { buffer_.put_i32(offset_ + PUB_LIMIT_COUNTER_ID_OFFSET, value); }

    [[nodiscard]] i32 channel_status_counter_id() const noexcept { return buffer_.get_i32(offset_ + CHANNEL_STATUS_COUNTER_ID_OFFSET); }
    void set_channel_status_counter_id(i32 value) noexcept { buffer_.put_i32(offset_ + CHANNEL_STATUS_COUNTER_ID_OFFSET, value); }

    [[nodiscard]] i32 log_file_name_length() const noexcept { return buffer_.get_i32(offset_ + LOG_FILE_NAME_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_log_file_name() for bounds-checked writes.
    void set_log_file_name_length(i32 length) noexcept { buffer_.put_i32(offset_ + LOG_FILE_NAME_LENGTH_OFFSET, length); }

    /// Returns a pointer to the log file name bytes. NOT null-terminated.
    [[nodiscard]] const char* log_file_name() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < LOG_FILE_NAME_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + LOG_FILE_NAME_OFFSET);
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

    /// Total byte length given the current log_file_name_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 len = log_file_name_length();
        if (len < 0) return -1;
        const i64 result = static_cast<i64>(LOG_FILE_NAME_OFFSET) + len;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for a given log file name string length.
    [[nodiscard]] static i32 compute_length(i32 log_file_name_length) noexcept
    {
        if (log_file_name_length < 0) return -1;
        const i64 result = static_cast<i64>(LOG_FILE_NAME_OFFSET) + log_file_name_length;
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
