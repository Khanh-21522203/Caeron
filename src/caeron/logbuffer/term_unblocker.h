#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"
#include "caeron/logbuffer/log_buffer_descriptor.h"
#include "caeron/protocol/header_flyweight.h"
#include "caeron/protocol/data_header_flyweight.h"

namespace caeron::logbuffer {

/// Status returned by TermUnblocker::unblock().
enum class UnblockStatus
{
    NO_ACTION,       ///< Not blocked — frame is complete or already processed
    UNBLOCKED,       ///< Successfully unblocked by converting to padding
    UNBLOCKED_TO_END ///< Unblocked to end of term (triggers term rotation)
};

/// Unblocks a stuck publisher that left a partial frame in the log.
///
/// If a publisher crashes or gets stuck mid-write, the term buffer has a partial
/// frame (negative length or zero length at the blocked offset). The unblocker
/// converts it to padding so the log can progress.
namespace TermUnblocker
{
    namespace detail
    {
        /// Write a padding frame header at the given offset.
        inline void write_padding_header(
            concurrent::UnsafeBuffer& log_meta_data_buffer,
            concurrent::UnsafeBuffer& term_buffer,
            i32 offset,
            i32 padding_length,
            i32 term_id)
        {
            LogBufferDescriptor::apply_default_header(
                log_meta_data_buffer, term_buffer, offset);
            FrameDescriptor::frame_type(
                term_buffer, offset, protocol::HeaderFlyweight::HDR_TYPE_PAD);
            FrameDescriptor::frame_term_offset(
                term_buffer, offset, offset);
            FrameDescriptor::frame_term_id(
                term_buffer, offset, term_id);
            FrameDescriptor::frame_length_ordered(
                term_buffer, offset, padding_length);
        }

        /// Verify all slots between `from` (exclusive) and `to` (inclusive) are zero.
        /// Walks backwards from `to` to `from`.
        inline bool scan_back_to_confirm_zeroed(
            const concurrent::UnsafeBuffer& term_buffer,
            i32 from,
            i32 to)
        {
            i32 offset = to;
            while (offset >= from)
            {
                if (FrameDescriptor::frame_length(term_buffer, offset) != 0)
                {
                    return false;
                }
                offset -= FrameDescriptor::FRAME_ALIGNMENT;
            }
            return true;
        }
    }

    /// Unblock a stuck publisher at the given offset.
    ///
    /// Three cases:
    ///   1. frame_length < 0 (partially written): CAS to convert to padding
    ///   2. frame_length == 0 (empty slot): scan forward, fill gap as padding
    ///   3. frame_length > 0 (complete frame): NO_ACTION
    ///
    /// @param log_meta_data_buffer  log metadata (for default header)
    /// @param term_buffer           the term buffer
    /// @param blocked_offset        the offset where the publisher is stuck
    /// @param tail_offset           the current tail offset
    /// @param term_id               the current term ID
    /// @return the unblock status
    [[nodiscard]] inline UnblockStatus unblock(
        concurrent::UnsafeBuffer& log_meta_data_buffer,
        concurrent::UnsafeBuffer& term_buffer,
        i32 blocked_offset,
        i32 tail_offset,
        i32 term_id)
    {
        const i32 frame_length = FrameDescriptor::frame_length(term_buffer, blocked_offset);

        // Case 1: Partially written frame (negative length)
        if (frame_length < 0)
        {
            const i32 abs_length = -frame_length;

            // CAS: atomically claim this frame for unblocking.
            // If the publisher completes its write between our read and CAS,
            // the CAS fails and we return NO_ACTION.
            if (!term_buffer.compare_and_set_i32(
                    blocked_offset, frame_length, abs_length))
            {
                return UnblockStatus::NO_ACTION;
            }

            // We won the CAS — write padding header.
            // apply_default_header copies 32 bytes (overwrites frame_length),
            // then frame_length_ordered writes the final value with release.
            detail::write_padding_header(
                log_meta_data_buffer, term_buffer, blocked_offset, abs_length, term_id);
            return UnblockStatus::UNBLOCKED;
        }

        // Case 2: Empty slot (frame_length == 0)
        if (frame_length == 0)
        {
            i32 current_offset = blocked_offset;

            // Scan forward looking for a non-zero frame, bounded by tail_offset
            while (current_offset < tail_offset)
            {
                if (FrameDescriptor::frame_length(term_buffer, current_offset) != 0)
                {
                    break;
                }
                current_offset += FrameDescriptor::FRAME_ALIGNMENT;
            }

            if (current_offset < tail_offset)
            {
                // Found a non-zero frame — verify all intermediate slots are still zero
                if (detail::scan_back_to_confirm_zeroed(
                        term_buffer, blocked_offset, current_offset - FrameDescriptor::FRAME_ALIGNMENT))
                {
                    const i32 padding_length = current_offset - blocked_offset;

                    // Guard against zero-length padding (concurrent publisher claimed
                    // the slot between our initial read and the forward scan).
                    if (padding_length == 0)
                    {
                        return UnblockStatus::NO_ACTION;
                    }

                    detail::write_padding_header(
                        log_meta_data_buffer, term_buffer, blocked_offset, padding_length, term_id);
                    return UnblockStatus::UNBLOCKED;
                }
            }
            else
            {
                // All slots to tail_offset are zero — fill to tail
                const i32 padding_length = tail_offset - blocked_offset;

                // Guard against zero-length padding (tail_offset == blocked_offset)
                if (padding_length == 0)
                {
                    return UnblockStatus::NO_ACTION;
                }

                detail::write_padding_header(
                    log_meta_data_buffer, term_buffer, blocked_offset, padding_length, term_id);
                return UnblockStatus::UNBLOCKED_TO_END;
            }
        }

        // Case 3: frame_length > 0 — not blocked
        return UnblockStatus::NO_ACTION;
    }
};

} // namespace caeron::logbuffer
