#pragma once

#include "unsafe_buffer.h"
#include "caeron/common/bit_util.h"

namespace caeron::concurrent {

/// Single-Producer broadcast transmitter.
///
/// DESIGN NOTE: This is a lossy broadcast. A single shared head position is used
/// for backpressure. A fast receiver can publish head progress that frees capacity
/// for the transmitter to overwrite data that a slow receiver hasn't read yet.
/// This matches Aeron's design — the to-clients broadcast buffer is used for
/// non-critical status/response messages where loss is acceptable.
///
/// Memory layout:
///   [0, 8)   - tail counter (i64, written by transmitter)
///   [8, 16)  - head counter (i64, written by receiver)
///   [16, 64) - reserved (cache-line padding)
///   [64, ..) - buffer data
///
/// Each record:
///   [0, 4) - length (i32, written last; negative = padding)
///   [4, 8) - message type ID (i32)
///   [8, 8+length) - message body
///
/// The tail counter is a monotonically increasing byte offset (not masked).
/// Consumers must mask it themselves to find the position in the buffer.
class BroadcastTransmitter
{
public:
    static constexpr i32 HEADER_LENGTH = CACHE_LINE_LENGTH; // 64
    static constexpr i32 MSG_HEADER_LENGTH = SIZE_OF_INT + SIZE_OF_INT; // 8
    static constexpr i32 PADDING_MSG_TYPE_ID = -1;

    explicit BroadcastTransmitter(UnsafeBuffer buffer)
        : buffer_{buffer}
    {
        const i32 cap = buffer_.capacity() - HEADER_LENGTH;
        if (cap <= 0)
            throw std::invalid_argument("buffer capacity must be greater than HEADER_LENGTH");
        if (!is_power_of_two(cap))
            throw std::invalid_argument("buffer capacity minus HEADER_LENGTH must be a power of two");

        capacity_ = cap;
        mask_ = capacity_ - 1;
    }

    /// Transmit a message to all receivers.
    ///
    /// @return true if the message was transmitted, false if not enough space.
    bool transmit(i32 msg_type_id, const void* src, i32 length)
    {
        if (msg_type_id == PADDING_MSG_TYPE_ID)
            throw std::invalid_argument("msg_type_id must not equal PADDING_MSG_TYPE_ID");
        if (length < 0)
            throw std::invalid_argument("message length must be non-negative");

        const i32 record_length = MSG_HEADER_LENGTH + length;
        const i32 required_capacity = align(record_length + SIZE_OF_INT, SIZE_OF_INT);

        const i64 tail = buffer_.get_i64_ordered(TAIL_POSITION_OFFSET);
        const i64 head = buffer_.get_i64_ordered(HEAD_POSITION_OFFSET);

        i32 tail_index = static_cast<i32>(tail & mask_);
        i32 to_buffer_end = capacity_ - tail_index;
        const i32 available = capacity_ - static_cast<i32>(tail - head);

        if (to_buffer_end < record_length + SIZE_OF_INT)
        {
            // Wrapping: need space for padding + record.
            const i32 claim = to_buffer_end + required_capacity;
            if (claim > available)
                return false;

            put_padding_record(tail_index, to_buffer_end);
            tail_index = 0;

            put_record(tail_index, record_length, msg_type_id, src, length);
            buffer_.put_i64_ordered(TAIL_POSITION_OFFSET, tail + claim);
        }
        else
        {
            if (required_capacity > available)
                return false;

            put_record(tail_index, record_length, msg_type_id, src, length);
            buffer_.put_i64_ordered(TAIL_POSITION_OFFSET, tail + required_capacity);
        }

        return true;
    }

private:
    static constexpr i32 TAIL_POSITION_OFFSET = 0;
    static constexpr i32 HEAD_POSITION_OFFSET = SIZE_OF_LONG; // 8

    void put_record(i32 index, i32 record_length,
                    i32 msg_type_id, const void* src, i32 length) noexcept
    {
        const i32 offset = HEADER_LENGTH + index;

        if (length > 0 && src != nullptr)
            buffer_.put_bytes(offset + MSG_HEADER_LENGTH, src, length);

        buffer_.put_i32(offset + SIZE_OF_INT, msg_type_id);
        buffer_.put_i32_ordered(offset, record_length);
    }

    void put_padding_record(i32 index, i32 to_buffer_end) noexcept
    {
        buffer_.put_i32_ordered(HEADER_LENGTH + index, -to_buffer_end);
    }

    UnsafeBuffer buffer_;
    i32 capacity_ = 0;
    i32 mask_ = 0;
};

} // namespace caeron::concurrent
