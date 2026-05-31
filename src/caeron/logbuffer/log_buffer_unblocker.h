#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/log_buffer_descriptor.h"
#include "caeron/logbuffer/term_unblocker.h"

namespace caeron::logbuffer {

/// Detects and unblocks stuck publishers across all three term partitions.
///
/// This is the higher-level wrapper that converts an absolute stream position
/// to a partition-local offset and delegates to TermUnblocker.
namespace LogBufferUnblocker
{
    /// Try to unblock a stuck publisher at the given position.
    ///
    /// Converts the absolute position to a partition-local offset, reads the
    /// tail counter for that partition, and delegates to TermUnblocker::unblock().
    /// If the blocked position is at the start of the next term, rotates the log.
    ///
    /// @param term_buffers          array of 3 term buffer UnsafeBuffers
    /// @param log_meta_data_buffer  the log metadata buffer
    /// @param blocked_position      the absolute stream position where the publisher is stuck
    /// @param term_length           the length of each term buffer
    /// @return true if unblocking occurred
    [[nodiscard]] inline bool unblock(
        concurrent::UnsafeBuffer term_buffers[],
        concurrent::UnsafeBuffer& log_meta_data_buffer,
        i64 blocked_position,
        i32 term_length)
    {
        const i32 position_bits_to_shift = LogBufferDescriptor::position_bits_to_shift(term_length);

        // Extract the term count and offset from the blocked position.
        // term_count = position >> position_bits_to_shift
        // term_offset = position & (term_length - 1)
        const i32 blocked_term_count = static_cast<i32>(
            static_cast<u64>(blocked_position) >> position_bits_to_shift);
        const i32 blocked_offset = static_cast<i32>(
            blocked_position & (term_length - 1));

        const i32 active_term_count = LogBufferDescriptor::active_term_count(log_meta_data_buffer);

        // If blocked at offset 0 of the next term, rotate the log
        if (blocked_offset == 0 && blocked_term_count == active_term_count + 1)
        {
            // Read the current term's ID from the current (not next) partition.
            // rotate_log expects the current term_id to compute the next term's ID.
            const i32 current_index = LogBufferDescriptor::index_by_term_count(active_term_count);
            const i64 current_tail = LogBufferDescriptor::raw_tail_volatile(log_meta_data_buffer, current_index);
            const i32 current_term_id = LogBufferDescriptor::tail_term_id(current_tail);

            return LogBufferDescriptor::rotate_log(log_meta_data_buffer, active_term_count, current_term_id);
        }

        // Normal case: delegate to TermUnblocker
        const i32 partition_index = LogBufferDescriptor::index_by_term_count(blocked_term_count);
        const i64 raw_tail = LogBufferDescriptor::raw_tail_volatile(log_meta_data_buffer, partition_index);
        const i32 term_id = LogBufferDescriptor::tail_term_id(raw_tail);
        const i32 tail_offset = LogBufferDescriptor::tail_term_offset(raw_tail);

        const UnblockStatus status = TermUnblocker::unblock(
            log_meta_data_buffer,
            term_buffers[partition_index],
            blocked_offset,
            tail_offset,
            term_id);

        if (status == UnblockStatus::UNBLOCKED_TO_END)
        {
            LogBufferDescriptor::rotate_log(log_meta_data_buffer, active_term_count, term_id);
            return true;
        }

        return status == UnblockStatus::UNBLOCKED;
    }
};

} // namespace caeron::logbuffer
