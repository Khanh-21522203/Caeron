#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i32  counter_type
///   [12] i32  key_buffer_length
///   [16] u8[] key_buffer (variable)
///   [16+key_buffer_length] i32  label_length
///   [20+key_buffer_length] u8[] label (variable)
class CounterMessageFlyweight {
public:
    static constexpr i32 COUNTER_TYPE_OFFSET    = 8;
    static constexpr i32 KEY_LENGTH_OFFSET      = 12;
    static constexpr i32 KEY_OFFSET             = 16;

    explicit CounterMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 counter_type() const noexcept { return buffer_.get_i32(offset_ + COUNTER_TYPE_OFFSET); }
    void set_counter_type(i32 value) noexcept { buffer_.put_i32(offset_ + COUNTER_TYPE_OFFSET, value); }

    [[nodiscard]] i32 key_buffer_length() const noexcept { return buffer_.get_i32(offset_ + KEY_LENGTH_OFFSET); }
    void set_key_buffer_length(i32 length) noexcept { buffer_.put_i32(offset_ + KEY_LENGTH_OFFSET, length); }

    [[nodiscard]] const char* key_buffer() const noexcept
    {
        return reinterpret_cast<const char*>(buffer_.data() + offset_ + KEY_OFFSET);
    }

    void set_key_buffer(const char* data, i32 length) noexcept
    {
        set_key_buffer_length(length);
        buffer_.put_bytes(offset_ + KEY_OFFSET, data, length);
    }

    [[nodiscard]] i32 label_length() const noexcept
    {
        return buffer_.get_i32(offset_ + KEY_OFFSET + key_buffer_length());
    }

    void set_label_length(i32 length) noexcept
    {
        buffer_.put_i32(offset_ + KEY_OFFSET + key_buffer_length(), length);
    }

    [[nodiscard]] const char* label() const noexcept
    {
        return reinterpret_cast<const char*>(
            buffer_.data() + offset_ + KEY_OFFSET + key_buffer_length() + 4);
    }

    void set_label(const char* data, i32 length) noexcept
    {
        set_label_length(length);
        buffer_.put_bytes(offset_ + KEY_OFFSET + key_buffer_length() + 4, data, length);
    }

    /// Total byte length of this flyweight given the current key_buffer_length() and label_length().
    [[nodiscard]] i32 length() const noexcept
    {
        return KEY_OFFSET + key_buffer_length() + 4 + label_length();
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
