#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Control message for removing a Publication.
/// Layout (matches Java Aeron RemovePublicationFlyweight):
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i64  registration_id
///   [24] i64  flags (bit 0 = revoke)
///   [32] total length = 32
inline constexpr i32 REMOVE_PUBLICATION_LENGTH = 32;
/// Legacy messages may omit the flags field (24 bytes: client_id + correlation_id + registration_id).
inline constexpr i32 REMOVE_PUBLICATION_LEGACY_LENGTH = 24;

class RemovePublicationFlyweight {
public:
    static constexpr i64 FLAG_REVOKE = 0x1;

    /// NOTE: offset_ is not bounds-checked. Callers must ensure offset_ >= 0
    /// and that the fixed fields (32 bytes) fit within the buffer capacity.
    explicit RemovePublicationFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 8); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 8, value); }

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + 16); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + 16, value); }

    [[nodiscard]] bool revoke() const noexcept
    {
        return (buffer_.get_i64(offset_ + 24) & FLAG_REVOKE) != 0;
    }

    /// Whether the revoke flag is valid given a message length.
    /// Older clients may not include the flags field.
    [[nodiscard]] bool revoke(i32 message_length) const noexcept
    {
        return message_length >= REMOVE_PUBLICATION_LENGTH && revoke();
    }

    void set_revoke(bool value) noexcept
    {
        i64 flags = buffer_.get_i64(offset_ + 24);
        if (value)
            flags |= FLAG_REVOKE;
        else
            flags &= ~FLAG_REVOKE;
        buffer_.put_i64(offset_ + 24, flags);
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
