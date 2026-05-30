#pragma once

#include "unsafe_buffer.h"
#include "caeron/common/bit_util.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace caeron::concurrent {

/// Manages counter allocation and deallocation backed by two buffers:
///   - Metadata buffer: stores type ID, key, and label per counter slot.
///   - Values buffer: stores an i64 value per counter slot.
///
/// Metadata slot layout (fixed size per counter):
///   [0, 4)  - type_id  (i32, 0 = free slot)
///   [4, 8)  - key_length (i32)
///   [8, 12) - label_length (i32)
///   [12, 12+key_length)     - key bytes
///   [12+key_length, 12+key_length+label_length) - label bytes (null-terminated)
///
/// Values layout:
///   counter_id * COUNTER_LENGTH -> i64 value
class CountersManager
{
public:
    static constexpr i32 COUNTER_LENGTH = SIZE_OF_LONG; // 8
    static constexpr i32 METADATA_TYPE_ID_OFFSET = 0;
    static constexpr i32 METADATA_KEY_LENGTH_OFFSET = 4;
    static constexpr i32 METADATA_LABEL_LENGTH_OFFSET = 8;
    static constexpr i32 METADATA_KEY_OFFSET = 12;
    static constexpr i32 FREE = 0;
    static constexpr i32 NOT_FREE = -1;

    /// @param metadata_buffer  Buffer for counter metadata. Capacity must be a multiple of
    ///                         METADATA_SLOT_SIZE.
    /// @param values_buffer    Buffer for counter values. Capacity must be a multiple of
    ///                         COUNTER_LENGTH.
    /// @param metadata_slot_size  Fixed size per metadata slot (default 64).
    CountersManager(UnsafeBuffer metadata_buffer, UnsafeBuffer values_buffer,
                    i32 metadata_slot_size = 64)
        : metadata_buffer_{metadata_buffer}
        , values_buffer_{values_buffer}
        , metadata_slot_size_{metadata_slot_size}
    {
        if (metadata_slot_size_ < METADATA_KEY_OFFSET)
            throw std::invalid_argument("metadata_slot_size too small");

        max_counter_id_ = values_buffer_.capacity() / COUNTER_LENGTH;
        if (max_counter_id_ != metadata_buffer_.capacity() / metadata_slot_size_)
            throw std::invalid_argument("metadata and values buffer have mismatched counter counts");
    }

    /// Allocate a counter slot.
    ///
    /// @param type_id      Type identifier for the counter.
    /// @param key          Optional key bytes (may be nullptr if key_length == 0).
    /// @param key_length   Length of the key in bytes.
    /// @param label        Null-terminated label string.
    /// @param label_length Length of the label in bytes (excluding null terminator, or including
    ///                     it -- the caller decides; we store exactly this many bytes).
    /// @return The allocated counter ID.
    i32 allocate(i32 type_id, const u8* key, i32 key_length,
                 const char* label, i32 label_length)
    {
        if (type_id == FREE || type_id == NOT_FREE)
            throw std::invalid_argument("type_id must not be FREE (0) or NOT_FREE (-1)");
        if (key_length < 0 || label_length < 0)
            throw std::invalid_argument("key_length and label_length must be non-negative");

        const i32 required_space = METADATA_KEY_OFFSET + key_length + label_length;
        if (required_space > metadata_slot_size_)
            throw std::invalid_argument("key + label do not fit in metadata slot");

        i32 counter_id = find_free_counter_id(type_id);
        if (counter_id == -1)
            throw std::runtime_error("no free counter slots available");

        const i32 slot_offset = counter_id * metadata_slot_size_;

        // Write key.
        metadata_buffer_.put_i32(slot_offset + METADATA_KEY_LENGTH_OFFSET, key_length);
        if (key_length > 0 && key != nullptr)
            metadata_buffer_.put_bytes(slot_offset + METADATA_KEY_OFFSET, key, key_length);

        // Write label.
        metadata_buffer_.put_i32(slot_offset + METADATA_LABEL_LENGTH_OFFSET, label_length);
        if (label_length > 0 && label != nullptr)
            metadata_buffer_.put_bytes(slot_offset + METADATA_KEY_OFFSET + key_length,
                                       label, label_length);

        // Zero the value.
        values_buffer_.put_i64(counter_id * COUNTER_LENGTH, 0);

        // Publish: write type_id last (release semantics).
        metadata_buffer_.put_i32_ordered(slot_offset + METADATA_TYPE_ID_OFFSET, type_id);

        return counter_id;
    }

    /// Free a counter slot. Sets type_id back to FREE and zeroes the value.
    void free(i32 counter_id)
    {
        validate_counter_id(counter_id);

        const i32 slot_offset = counter_id * metadata_slot_size_;
        metadata_buffer_.put_i32_ordered(slot_offset + METADATA_TYPE_ID_OFFSET, FREE);
        values_buffer_.put_i64_volatile(counter_id * COUNTER_LENGTH, 0);
    }

    /// Get the counter value (const).
    /// Uses volatile load for atomic read access. For atomic RMW operations,
    /// use AtomicCounter which wraps the same memory location.
    [[nodiscard]] i64 get_counter_value(i32 counter_id) const
    {
        validate_counter_id(counter_id);
        return values_buffer_.get_i64_volatile(counter_id * COUNTER_LENGTH);
    }

    /// Set the counter value.
    void set_counter_value(i32 counter_id, i64 value)
    {
        validate_counter_id(counter_id);
        values_buffer_.put_i64_volatile(counter_id * COUNTER_LENGTH, value);
    }

    /// Get the type ID of a counter slot.
    /// Uses acquire semantics to synchronize with the release store in allocate().
    [[nodiscard]] i32 get_type_id(i32 counter_id) const
    {
        validate_counter_id(counter_id);
        return metadata_buffer_.get_i32_ordered(
            counter_id * metadata_slot_size_ + METADATA_TYPE_ID_OFFSET);
    }

    /// Get the key bytes for a counter slot.
    /// Returns the number of bytes copied into dst (up to dst_length).
    [[nodiscard]] i32 get_key(i32 counter_id, u8* dst, i32 dst_length) const
    {
        validate_counter_id(counter_id);
        const i32 slot_offset = counter_id * metadata_slot_size_;
        const i32 key_length = metadata_buffer_.get_i32(
            slot_offset + METADATA_KEY_LENGTH_OFFSET);
        const i32 copy_len = std::min(key_length, dst_length);
        if (copy_len > 0 && dst != nullptr)
            metadata_buffer_.get_bytes(slot_offset + METADATA_KEY_OFFSET, dst, copy_len);
        return key_length;
    }

    /// Get the label for a counter slot.
    /// Returns the number of bytes copied into dst (up to dst_length).
    [[nodiscard]] i32 get_label(i32 counter_id, char* dst, i32 dst_length) const
    {
        validate_counter_id(counter_id);
        const i32 slot_offset = counter_id * metadata_slot_size_;
        const i32 key_length = metadata_buffer_.get_i32(
            slot_offset + METADATA_KEY_LENGTH_OFFSET);
        const i32 label_length = metadata_buffer_.get_i32(
            slot_offset + METADATA_LABEL_LENGTH_OFFSET);
        const i32 copy_len = std::min(label_length, dst_length);
        if (copy_len > 0 && dst != nullptr)
            metadata_buffer_.get_bytes(slot_offset + METADATA_KEY_OFFSET + key_length,
                                       dst, copy_len);
        return label_length;
    }

    /// Iterate over all allocated counters. Calls handler(counter_id, type_id)
    /// for each allocated slot.
    template <typename Handler>
    void forEach(Handler&& handler) const
    {
        for (i32 i = 0; i < max_counter_id_; ++i)
        {
            // Acquire load to synchronize with the release store in allocate().
            const i32 type_id = metadata_buffer_.get_i32_ordered(
                i * metadata_slot_size_ + METADATA_TYPE_ID_OFFSET);
            // Skip FREE (0) and NOT_FREE (-1, in-progress allocation).
            if (type_id > 0)
                handler(i, type_id);
        }
    }

    [[nodiscard]] i32 max_counter_id() const noexcept { return max_counter_id_; }

    /// Get the offset into the values buffer for a given counter.
    [[nodiscard]] static constexpr i32 counter_offset(i32 counter_id) noexcept
    {
        return counter_id * COUNTER_LENGTH;
    }

private:
    i32 find_free_counter_id(i32 type_id)
    {
        for (i32 i = 0; i < max_counter_id_; ++i)
        {
            const i32 slot_offset = i * metadata_slot_size_;
            const i32 existing = metadata_buffer_.get_i32_volatile(
                slot_offset + METADATA_TYPE_ID_OFFSET);
            if (existing == FREE)
            {
                // Mark as allocated so other threads don't take it.
                if (metadata_buffer_.compare_and_set_i32(
                        slot_offset + METADATA_TYPE_ID_OFFSET, FREE, NOT_FREE))
                    return i;
            }
        }
        return -1;
    }

    void validate_counter_id(i32 counter_id) const
    {
        if (counter_id < 0 || counter_id >= max_counter_id_)
            throw std::out_of_range("counter_id out of range");
    }

    UnsafeBuffer metadata_buffer_;
    UnsafeBuffer values_buffer_;
    i32 metadata_slot_size_;
    i32 max_counter_id_ = 0;
};

} // namespace caeron::concurrent
