#pragma once

#include "unsafe_buffer.h"
#include "caeron/common/bit_util.h"

namespace caeron::concurrent {

/// Single-Consumer broadcast receiver.
///
/// Reads from the same buffer layout as BroadcastTransmitter.
/// Caches the current read position (tail_ tracks how far we have read).
///
/// Memory layout:
///   [0, 64)  - tail counter written by transmitter (i64)
///   [64, ..) - buffer data
///
/// Each record:
///   [0, 4) - length (i32; negative = padding)
///   [4, 8) - message type ID (i32)
///   [8, ..) - message body
class BroadcastReceiver
{
public:
    static constexpr i32 HEADER_LENGTH = CACHE_LINE_LENGTH; // 64
    static constexpr i32 MSG_HEADER_LENGTH = SIZE_OF_INT + SIZE_OF_INT; // 8
    static constexpr i32 PADDING_MSG_TYPE_ID = -1;

    explicit BroadcastReceiver(UnsafeBuffer buffer)
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

    /// Receive messages from the buffer.
    ///
    /// The handler is called as: handler(msg_type_id, buffer_ptr, length)
    /// Returns the number of messages received.
    template <typename Handler>
    i32 receive(Handler&& handler)
    {
        const i64 tail = buffer_.get_i64_ordered(TAIL_POSITION_OFFSET);
        i64 next_tail = tail_;
        i32 messages_received = 0;
        i32 index = static_cast<i32>(next_tail & mask_);

        while (next_tail < tail)
        {
            const i32 record_length = buffer_.get_i32_ordered(index);

            if (record_length <= 0)
            {
                // Padding record: skip to start of buffer.
                next_tail = (next_tail - index) + capacity_;
                index = 0;
                continue;
            }

            const i32 msg_type_id = buffer_.get_i32(index + SIZE_OF_INT);
            const i32 msg_length = record_length - MSG_HEADER_LENGTH;

            handler(msg_type_id,
                    reinterpret_cast<const std::byte*>(buffer_.data()) + index + MSG_HEADER_LENGTH,
                    msg_length);

            next_tail += align(record_length + SIZE_OF_INT, SIZE_OF_INT);
            index = static_cast<i32>(next_tail & mask_);
            ++messages_received;
        }

        tail_ = next_tail;
        return messages_received;
    }

private:
    static constexpr i32 TAIL_POSITION_OFFSET = 0;

    UnsafeBuffer buffer_;
    i32 capacity_ = 0;
    i32 mask_ = 0;
    i64 tail_ = 0;
};

} // namespace caeron::concurrent
