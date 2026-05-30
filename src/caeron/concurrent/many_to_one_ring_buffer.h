#pragma once

#include "unsafe_buffer.h"
#include "caeron/common/bit_util.h"

#include <cstring>
#include <functional>

namespace caeron::concurrent {

/// Lock-free Many-to-One (MPSC) ring buffer.
///
/// Memory layout:
///   [0, 64)   - head position (i64, cache-line padded)
///   [64, 128) - tail position (i64, cache-line padded)
///   [128, ..) - buffer data
///
/// Each message record:
///   [0, 4) - length field (i32, written last by producer; negative = padding)
///   [4, 8) - message type ID (i32)
///   [8, 8+length) - message body
class ManyToOneRingBuffer
{
public:
    static constexpr i32 HEADER_LENGTH = 2 * CACHE_LINE_LENGTH; // 128
    static constexpr i32 MSG_HEADER_LENGTH = SIZE_OF_INT + SIZE_OF_INT; // 8 (length + type_id)
    static constexpr i32 PADDING_MSG_TYPE_ID = -1;

    explicit ManyToOneRingBuffer(UnsafeBuffer buffer)
        : buffer_{buffer}
    {
        const i32 cap = buffer_.capacity() - HEADER_LENGTH;
        if (cap <= 0)
            throw std::invalid_argument("buffer capacity must be greater than HEADER_LENGTH");
        if (!is_power_of_two(cap))
            throw std::invalid_argument("buffer capacity minus HEADER_LENGTH must be a power of two");

        capacity_ = cap;
        max_msg_length_ = std::min(cap / 8, capacity_);
        mask_ = capacity_ - 1;
    }

    /// Capacity of the data region (excluding header).
    [[nodiscard]] i32 capacity() const noexcept { return capacity_; }

    /// Approximate number of bytes currently in the ring buffer.
    [[nodiscard]] i32 size() const noexcept
    {
        const i64 head_after = buffer_.get_i64_ordered(HEAD_POSITION_OFFSET);
        const i64 tail_after = buffer_.get_i64_ordered(TAIL_POSITION_OFFSET);

        i64 size = tail_after - head_after;
        if (size < 0)
            size = 0;
        return static_cast<i32>(size);
    }

    /// Write a message into the ring buffer.
    ///
    /// @return true if the message was written, false if there is not enough space.
    bool write(i32 msg_type_id, const void* src, i32 length)
    {
        if (msg_type_id == PADDING_MSG_TYPE_ID)
            throw std::invalid_argument("msg_type_id must not equal PADDING_MSG_TYPE_ID");
        if (length < 0)
            throw std::invalid_argument("message length must be non-negative");

        const i32 record_length = MSG_HEADER_LENGTH + length;
        if (record_length > max_msg_length_)
            return false;

        // Aligned space needed for the record plus the next record's length header.
        const i32 required_capacity = align(record_length + SIZE_OF_INT, SIZE_OF_INT);
        i64 tail;
        i64 next_tail;

        while (true)
        {
            tail = buffer_.get_i64_ordered(TAIL_POSITION_OFFSET);
            const i64 head = buffer_.get_i64_ordered(HEAD_POSITION_OFFSET);

            const i32 tail_index = static_cast<i32>(tail & mask_);
            const i32 to_buffer_end = capacity_ - tail_index;
            const i32 available = capacity_ - static_cast<i32>(tail - head);

            if (to_buffer_end < record_length + SIZE_OF_INT)
            {
                // Wrapping: the claim includes padding bytes at the end of the buffer
                // plus the record at the start.
                const i32 claim = to_buffer_end + required_capacity;
                if (claim > available)
                    return false;
                next_tail = tail + claim;
            }
            else
            {
                if (required_capacity > available)
                    return false;
                next_tail = tail + required_capacity;
            }

            if (buffer_.compare_and_set_i64(TAIL_POSITION_OFFSET, tail, next_tail))
                break;
        }

        const i32 tail_index = static_cast<i32>(tail & mask_);
        const i32 to_buffer_end = capacity_ - tail_index;

        // If the record would wrap around, write a padding record first.
        if (to_buffer_end < record_length + SIZE_OF_INT)
        {
            // Fill the remaining space with a padding record (negative length).
            put_padding_record(tail_index, to_buffer_end);
            // Write the actual record at the start of the buffer.
            put_msg_header_and_body(0, record_length, msg_type_id, src, length);
        }
        else
        {
            put_msg_header_and_body(tail_index, record_length, msg_type_id, src, length);
        }

        return true;
    }

    /// Read all available messages from the ring buffer.
    ///
    /// The handler is called as: handler(msg_type_id, buffer_ptr, length)
    /// Returns the number of messages read.
    template <typename Handler>
    i32 read(Handler&& handler)
    {
        const i64 head = buffer_.get_i64_ordered(HEAD_POSITION_OFFSET);
        i64 next_head = head;
        const i64 current_tail = buffer_.get_i64_ordered(TAIL_POSITION_OFFSET);

        i32 messages_read = 0;
        i32 head_index = static_cast<i32>(next_head & mask_);

        while (next_head < current_tail)
        {
            // head_index is offset within data region; add HEADER_LENGTH for absolute offset.
            const i32 offset = HEADER_LENGTH + head_index;
            const i32 record_length = buffer_.get_i32_ordered(offset);

            if (record_length == 0)
                break; // Unpublished record — producer CAS'd tail but hasn't written yet.

            if (record_length < 0)
            {
                // Padding record: clear it to prevent stale negative length after wrap-around.
                buffer_.put_i32_ordered(offset, 0);
                // Skip to the start of the buffer.
                next_head = (next_head - head_index) + capacity_;
                head_index = 0;
                continue;
            }

            if (record_length > max_msg_length_)
                throw std::runtime_error("corrupt ring buffer record: length exceeds max");

            const i32 msg_type_id = buffer_.get_i32(offset + SIZE_OF_INT);
            const i32 msg_length = record_length - MSG_HEADER_LENGTH;

            handler(msg_type_id,
                    reinterpret_cast<const std::byte*>(buffer_.data()) + offset + MSG_HEADER_LENGTH,
                    msg_length);

            // Clear consumed record length to prevent stale data after wrap-around.
            // Without this, a reused slot (producer CAS'd tail but hasn't written yet)
            // would contain the old positive length, and the consumer would read garbage.
            buffer_.put_i32_ordered(offset, 0);

            next_head += align(record_length + SIZE_OF_INT, SIZE_OF_INT);
            head_index = static_cast<i32>(next_head & mask_);
            ++messages_read;
        }

        // Publish head progress whenever we advanced, even if no messages were delivered.
        // This prevents the consumer from getting stuck when it clears padding but the
        // wrapped record hasn't been published yet (record_length == 0). Without this,
        // the next read would restart at the old head (now zeroed padding) and loop forever.
        if (next_head != head)
        {
            buffer_.put_i64_ordered(HEAD_POSITION_OFFSET, next_head);
        }

        return messages_read;
    }

private:
    static constexpr i32 HEAD_POSITION_OFFSET = 0;
    static constexpr i32 TAIL_POSITION_OFFSET = CACHE_LINE_LENGTH;

    void put_msg_header_and_body(i32 index, i32 record_length,
                                 i32 msg_type_id, const void* src, i32 length) noexcept
    {
        // index is offset within data region; add HEADER_LENGTH for absolute buffer offset.
        const i32 offset = HEADER_LENGTH + index;

        // Zero out the body portion.
        buffer_.set_memory(offset + MSG_HEADER_LENGTH, record_length - MSG_HEADER_LENGTH, 0);

        if (length > 0 && src != nullptr)
            buffer_.put_bytes(offset + MSG_HEADER_LENGTH, src, length);

        // Write type_id.
        buffer_.put_i32(offset + SIZE_OF_INT, msg_type_id);
        // Write length last (release semantics) to publish the message.
        buffer_.put_i32_ordered(offset, record_length);
    }

    void put_padding_record(i32 index, i32 to_buffer_end) noexcept
    {
        // Negative length signals padding. The reader will skip to buffer start.
        buffer_.put_i32_ordered(HEADER_LENGTH + index, -to_buffer_end);
    }

    UnsafeBuffer buffer_;
    i32 capacity_ = 0;
    i32 mask_ = 0;
    i32 max_msg_length_ = 0;
};

} // namespace caeron::concurrent
