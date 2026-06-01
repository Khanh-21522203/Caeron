#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Publication error frame from driver to client.
/// Layout (matches Java Aeron PublicationErrorFlyweight):
///   [ 0] i64  registration_id
///   [ 8] i64  destination_registration_id
///   [16] i32  session_id
///   [20] i32  stream_id
///   [24] i64  receiver_id
///   [32] i64  group_tag
///   [40] i16  address_type
///   [42] i16  udp_port
///   [44] u8[16] address (IPv4 or IPv6)
///   [60] i32  error_code
///   [64] i32  error_message_length
///   [68] u8[] error_message (variable)
inline constexpr i32 PUBLICATION_ERROR_FRAME_LENGTH = 68; // minimum fixed part

class PublicationErrorFrameFlyweight {
public:
    static constexpr i32 REGISTRATION_ID_OFFSET                = 0;
    static constexpr i32 DESTINATION_REGISTRATION_ID_OFFSET    = 8;
    static constexpr i32 SESSION_ID_OFFSET                     = 16;
    static constexpr i32 STREAM_ID_OFFSET                      = 20;
    static constexpr i32 RECEIVER_ID_OFFSET                    = 24;
    static constexpr i32 GROUP_TAG_OFFSET                      = 32;
    static constexpr i32 ADDRESS_TYPE_OFFSET                   = 40;
    static constexpr i32 UDP_PORT_OFFSET                       = 42;
    static constexpr i32 ADDRESS_OFFSET                        = 44;
    static constexpr i32 ADDRESS_LENGTH                        = 16;
    static constexpr i32 ERROR_CODE_OFFSET                     = 60;
    static constexpr i32 ERROR_MESSAGE_LENGTH_OFFSET           = 64;
    static constexpr i32 ERROR_MESSAGE_OFFSET                  = 68;

    explicit PublicationErrorFrameFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + REGISTRATION_ID_OFFSET); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i64 destination_registration_id() const noexcept { return buffer_.get_i64(offset_ + DESTINATION_REGISTRATION_ID_OFFSET); }
    void set_destination_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + DESTINATION_REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i32 session_id() const noexcept { return buffer_.get_i32(offset_ + SESSION_ID_OFFSET); }
    void set_session_id(i32 value) noexcept { buffer_.put_i32(offset_ + SESSION_ID_OFFSET, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    [[nodiscard]] i64 receiver_id() const noexcept { return buffer_.get_i64(offset_ + RECEIVER_ID_OFFSET); }
    void set_receiver_id(i64 value) noexcept { buffer_.put_i64(offset_ + RECEIVER_ID_OFFSET, value); }

    [[nodiscard]] i64 group_tag() const noexcept { return buffer_.get_i64(offset_ + GROUP_TAG_OFFSET); }
    void set_group_tag(i64 value) noexcept { buffer_.put_i64(offset_ + GROUP_TAG_OFFSET, value); }

    [[nodiscard]] i16 address_type() const noexcept { return buffer_.get_i16(offset_ + ADDRESS_TYPE_OFFSET); }
    void set_address_type(i16 value) noexcept { buffer_.put_i16(offset_ + ADDRESS_TYPE_OFFSET, value); }

    [[nodiscard]] i16 udp_port() const noexcept { return buffer_.get_i16(offset_ + UDP_PORT_OFFSET); }
    void set_udp_port(i16 value) noexcept { buffer_.put_i16(offset_ + UDP_PORT_OFFSET, value); }

    /// Returns a pointer to the 16-byte address field (IPv4 or IPv6 bytes).
    [[nodiscard]] const u8* address() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ADDRESS_OFFSET + ADDRESS_LENGTH) return nullptr;
        return reinterpret_cast<const u8*>(buffer_.data() + offset_ + ADDRESS_OFFSET);
    }

    void set_address(const void* src, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ADDRESS_OFFSET + ADDRESS_LENGTH) return;
        if (length > ADDRESS_LENGTH)
            length = ADDRESS_LENGTH;
        if (src != nullptr && length > 0)
        {
            buffer_.put_bytes(offset_ + ADDRESS_OFFSET, src, length);
            // Zero any remaining bytes for a clean 16-byte field
            if (length < ADDRESS_LENGTH)
                buffer_.set_memory(offset_ + ADDRESS_OFFSET + length, ADDRESS_LENGTH - length, 0);
        }
        else
            buffer_.set_memory(offset_ + ADDRESS_OFFSET, ADDRESS_LENGTH, 0);
    }

    [[nodiscard]] i32 error_code() const noexcept { return buffer_.get_i32(offset_ + ERROR_CODE_OFFSET); }
    void set_error_code(i32 value) noexcept { buffer_.put_i32(offset_ + ERROR_CODE_OFFSET, value); }

    [[nodiscard]] i32 error_message_length() const noexcept { return buffer_.get_i32(offset_ + ERROR_MESSAGE_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_error_message() for bounds-checked writes.
    void set_error_message_length(i32 length) noexcept { buffer_.put_i32(offset_ + ERROR_MESSAGE_LENGTH_OFFSET, length); }

    /// Returns a pointer to the error message bytes. NOT null-terminated.
    [[nodiscard]] const char* error_message() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ERROR_MESSAGE_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + ERROR_MESSAGE_OFFSET);
    }

    /// Copy error message bytes into the buffer.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_error_message(const char* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < ERROR_MESSAGE_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - ERROR_MESSAGE_OFFSET) return;
        set_error_message_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(offset_ + ERROR_MESSAGE_OFFSET, data, length);
    }

    /// Total byte length given the current error_message_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 len = error_message_length();
        if (len < 0) return -1;
        const i64 result = static_cast<i64>(ERROR_MESSAGE_OFFSET) + len;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for a given error message string length.
    [[nodiscard]] static i32 compute_length(i32 error_message_length) noexcept
    {
        if (error_message_length < 0) return -1;
        const i64 result = static_cast<i64>(ERROR_MESSAGE_OFFSET) + error_message_length;
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
