#pragma once

#include "caeron/common/bit_util.h"
#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>

namespace caeron::command {

/// Layout (matches Java Aeron CounterMessageFlyweight):
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i32  counter_type
///   [20] i32  key_buffer_length
///   [24] u8[] key_buffer (variable)
///   [24+key_buffer_length] i32  label_length
///   [28+key_buffer_length] u8[] label (variable)
class CounterMessageFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET      = 0;
    static constexpr i32 CORRELATION_ID_OFFSET = 8;
    static constexpr i32 COUNTER_TYPE_OFFSET    = 16;
    static constexpr i32 KEY_LENGTH_OFFSET      = 20;
    static constexpr i32 KEY_OFFSET             = 24;

    explicit CounterMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i32 counter_type() const noexcept { return buffer_.get_i32(offset_ + COUNTER_TYPE_OFFSET); }
    void set_counter_type(i32 value) noexcept { buffer_.put_i32(offset_ + COUNTER_TYPE_OFFSET, value); }

    [[nodiscard]] i32 key_buffer_length() const noexcept { return buffer_.get_i32(offset_ + KEY_LENGTH_OFFSET); }
    /// RAW FIELD SETTER -- no validation. Prefer set_key_buffer() for bounds-checked writes.
    void set_key_buffer_length(i32 length) noexcept { buffer_.put_i32(offset_ + KEY_LENGTH_OFFSET, length); }

    [[nodiscard]] const char* key_buffer() const noexcept
    {
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return nullptr;
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + KEY_OFFSET);
    }

    /// Offset of key buffer data from the flyweight start.
    [[nodiscard]] i32 key_buffer_offset() const noexcept { return KEY_OFFSET; }

    /// Copy key buffer bytes into the flyweight.
    ///
    /// Contract:
    ///   - Negative length is a no-op.
    ///   - If data is nullptr, the length field is set but no bytes are written
    ///     (null-to-empty coercion). This avoids dereferencing a null pointer.
    ///   - If length is zero, no bytes are written regardless of data.
    void set_key_buffer(const void* data, i32 length) noexcept
    {
        if (length < 0) return;
        if (offset_ < 0 || buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return;
        if (length > buffer_.capacity() - offset_ - KEY_OFFSET) return;
        set_key_buffer_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(offset_ + KEY_OFFSET, data, length);
    }

    [[nodiscard]] i32 label_length() const noexcept
    {
        if (offset_ < 0) return -1;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return -1;
        const i32 kl = key_buffer_length();
        if (kl < 0) return -1;
        if (kl > buffer_.capacity() - offset_ - KEY_OFFSET - SIZE_OF_INT) return -1;
        return buffer_.get_i32(offset_ + KEY_OFFSET + kl);
    }

    /// RAW FIELD SETTER -- no validation. Prefer set_label() for bounds-checked writes.
    void set_label_length(i32 length) noexcept
    {
        if (offset_ < 0) return;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return;
        const i32 kl = key_buffer_length();
        if (kl < 0) return;
        if (kl > buffer_.capacity() - offset_ - KEY_OFFSET - SIZE_OF_INT) return;
        buffer_.put_i32(offset_ + KEY_OFFSET + kl, length);
    }

    /// Offset of label buffer data from the flyweight start.
    [[nodiscard]] i32 label_buffer_offset() const noexcept
    {
        if (offset_ < 0) return -1;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET + SIZE_OF_INT) return -1;
        const i32 kl = key_buffer_length();
        if (kl < 0 || kl > buffer_.capacity() - offset_ - KEY_OFFSET - SIZE_OF_INT) return -1;
        return KEY_OFFSET + kl + SIZE_OF_INT;  // safe: bounds proven above
    }

    [[nodiscard]] const char* label() const noexcept
    {
        if (offset_ < 0) return nullptr;
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return nullptr;
        const i32 kl = key_buffer_length();
        if (kl < 0) return nullptr;
        if (kl > buffer_.capacity() - offset_ - KEY_OFFSET - SIZE_OF_INT) return nullptr;
        return reinterpret_cast<const char*>(
            buffer_.data() + offset_ + KEY_OFFSET + kl + SIZE_OF_INT);
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
        if (buffer_.capacity() < offset_ || buffer_.capacity() - offset_ < KEY_OFFSET) return;
        const i32 kl = key_buffer_length();
        if (kl < 0) return;
        if (kl > buffer_.capacity() - offset_ - KEY_OFFSET - SIZE_OF_INT) return;
        const i32 label_data_offset = offset_ + KEY_OFFSET + kl + SIZE_OF_INT;
        if (length > buffer_.capacity() - label_data_offset) return;
        set_label_length(length);
        if (data != nullptr && length > 0)
            buffer_.put_bytes(label_data_offset, data, length);
    }

    /// Total byte length of this flyweight given the current key_buffer_length() and label_length().
    [[nodiscard]] i32 length() const noexcept
    {
        const i32 kl = key_buffer_length();
        if (kl < 0) return -1;
        const i32 ll = label_length();
        if (ll < 0) return -1;
        const i64 result = static_cast<i64>(KEY_OFFSET) + kl + 4 + ll;
        if (result > std::numeric_limits<i32>::max()) return -1;
        return static_cast<i32>(result);
    }

    /// Compute the total byte length for given key and label lengths.
    [[nodiscard]] static i32 compute_length(i32 key_length, i32 label_length) noexcept
    {
        if (key_length < 0 || label_length < 0) return -1;
        const i64 result = static_cast<i64>(KEY_OFFSET) + key_length + 4 + label_length;
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
