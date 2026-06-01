#pragma once

#include "caeron/common/bit_util.h"
#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Message to get or create a new static counter.
/// Extends CorrelatedMessageFlyweight in Java; standalone here.
/// Layout (SBE 2.0 compliant: label length is 4-byte aligned):
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i64  registration_id
///   [24] i32  counter_type_id
///   [28] i32  key_length
///   [32] u8[] key_buffer (variable, 4-byte aligned)
///   [32 + align(key_length, 4)] i32  label_length
///   [36 + align(key_length, 4)] u8[] label (variable)
inline constexpr i32 STATIC_COUNTER_MSG_MINIMUM_LENGTH = 36;

class StaticCounterMessageFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET       = 0;
    static constexpr i32 CORRELATION_ID_OFFSET  = 8;
    static constexpr i32 REGISTRATION_ID_OFFSET = 16;
    static constexpr i32 COUNTER_TYPE_ID_OFFSET = 24;
    static constexpr i32 KEY_LENGTH_OFFSET      = 28;
    static constexpr i32 KEY_BUFFER_OFFSET      = 32;

    explicit StaticCounterMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + REGISTRATION_ID_OFFSET); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i32 counter_type_id() const noexcept { return buffer_.get_i32(offset_ + COUNTER_TYPE_ID_OFFSET); }
    void set_counter_type_id(i32 value) noexcept { buffer_.put_i32(offset_ + COUNTER_TYPE_ID_OFFSET, value); }

    [[nodiscard]] i32 key_length() const noexcept { return buffer_.get_i32(offset_ + KEY_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_key_buffer() for bounds-checked writes.
    void set_key_length(i32 length) noexcept { buffer_.put_i32(offset_ + KEY_LENGTH_OFFSET, length); }

    [[nodiscard]] const u8* key_buffer() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_BUFFER_OFFSET) return nullptr;
        return reinterpret_cast<const u8*>(buffer_.data() + offset_ + KEY_BUFFER_OFFSET);
    }

    /// Copy key buffer bytes into the flyweight, with 4-byte-aligned padding.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    ///   - Padding bytes between key data and the aligned label_length field
    ///     are zeroed to avoid leaking stale scratch-buffer data.
    void set_key_buffer(const void* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0) return;
        // Guard against overflow in align() (noexcept context).
        if (length > std::numeric_limits<i32>::max() - (SIZE_OF_INT - 1)) return;
        const i32 aligned_end = align(length, SIZE_OF_INT);
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_BUFFER_OFFSET) return;
        if (aligned_end > buffer_.capacity() - offset_ - KEY_BUFFER_OFFSET) return;
        set_key_length(length);
        if (data != nullptr && length > 0)
        {
            buffer_.put_bytes(offset_ + KEY_BUFFER_OFFSET, data, length);
        }
        // Zero padding bytes between key data and the aligned label_length field
        // to avoid leaking stale data from the scratch buffer.
        if (aligned_end > length)
        {
            buffer_.set_memory(offset_ + KEY_BUFFER_OFFSET + length, aligned_end - length, 0);
        }
    }

    /// Offset of the label length field (4-byte aligned after key buffer).
    /// Returns -1 if key_length() is negative, would overflow when aligned,
    /// or would exceed the buffer capacity.
    [[nodiscard]] i32 label_length_offset() const noexcept
    {
        if (offset_ < 0) return -1;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_BUFFER_OFFSET + SIZE_OF_INT) return -1;
        const i32 kl = key_length();
        if (kl < 0) return -1;
        // Guard against overflow in align() (noexcept context).
        if (kl > std::numeric_limits<i32>::max() - (SIZE_OF_INT - 1)) return -1;
        const i32 aligned = align(kl, SIZE_OF_INT);
        if (aligned > buffer_.capacity() - offset_ - KEY_BUFFER_OFFSET - SIZE_OF_INT) return -1;
        return KEY_BUFFER_OFFSET + aligned;  // safe: bounds proven above
    }

    [[nodiscard]] i32 label_length() const noexcept
    {
        if (offset_ < 0) return -1;
        const i32 llo = label_length_offset();
        if (llo < 0) return -1;
        return buffer_.get_i32(offset_ + llo);
    }

    /// RAW FIELD SETTER -- no validation. Prefer set_label() for bounds-checked writes.
    void set_label_length(i32 length) noexcept
    {
        if (offset_ < 0) return;
        const i32 llo = label_length_offset();
        if (llo < 0) return;
        buffer_.put_i32(offset_ + llo, length);
    }

    /// Offset of the label data (immediately after label length field).
    /// Returns -1 if key_length() is negative (corrupt header).
    [[nodiscard]] i32 label_buffer_offset() const noexcept
    {
        if (offset_ < 0) return -1;
        const i32 llo = label_length_offset();
        if (llo < 0) return -1;
        return llo + SIZE_OF_INT;
    }

    [[nodiscard]] const char* label() const noexcept
    {
        if (offset_ < 0) return nullptr;
        const i32 lbo = label_buffer_offset();
        if (lbo < 0) return nullptr;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < lbo) return nullptr;
        return reinterpret_cast<const char*>(
            buffer_.data() + offset_ + lbo);
    }

    /// Copy label bytes into the flyweight.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_label(const void* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0) return;
        const i32 lbo = label_buffer_offset();
        if (lbo < 0) return;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < lbo) return;
        if (length > buffer_.capacity() - offset_ - lbo) return;
        set_label_length(length);
        if (data != nullptr && length > 0)
        {
            buffer_.put_bytes(offset_ + lbo, data, length);
        }
    }

    /// Total byte length of this flyweight given the current key_length() and label_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 kl = key_length();
        if (kl < 0) return -1;
        const i32 ll = label_length();
        if (ll < 0) return -1;
        const i32 lbo = label_buffer_offset();
        if (lbo < 0) return -1;
        const i64 result = static_cast<i64>(lbo) + ll;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for given key and label lengths.
    [[nodiscard]] static i32 compute_length(i32 key_length, i32 label_length) noexcept
    {
        if (key_length < 0 || label_length < 0) return -1;
        if (key_length > std::numeric_limits<i32>::max() - SIZE_OF_INT) return -1;
        const i32 aligned_key = align(key_length, SIZE_OF_INT); // may throw on overflow
        const i64 result = static_cast<i64>(KEY_BUFFER_OFFSET) + aligned_key + SIZE_OF_INT + label_length;
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
