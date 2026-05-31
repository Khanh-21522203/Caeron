#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"
#include "caeron/logbuffer/log_buffer_descriptor.h"
#include "caeron/protocol/header_flyweight.h"
#include "caeron/protocol/data_header_flyweight.h"

namespace caeron::logbuffer {

/// Fills a gap in the term buffer with a padding frame so the log can progress.
///
/// When a gap has been detected and NAKed but the data hasn't arrived in time,
/// the gap is filled with a padding frame. This prevents the subscriber from
/// being stuck waiting for missing data.
namespace TermGapFiller
{
    /// Try to fill a gap with a padding frame.
    ///
    /// Walks backwards from the end of the gap to the start, verifying that all
    /// slots are still empty (zero). If any slot has been written to (data arrived
    /// concurrently), returns false without modifying the buffer.
    ///
    /// If all slots are empty, fills the gap with a single padding frame:
    ///   1. Apply the default header from the metadata buffer
    ///   2. Set the frame type to PAD
    ///   3. Write term_offset and term_id
    ///   4. Write frame_length with release semantics (publishes the padding)
    ///
    /// @param log_meta_data_buffer  the log metadata buffer (contains default header)
    /// @param term_buffer           the term buffer containing the gap
    /// @param term_id               the current term ID
    /// @param gap_offset            the byte offset where the gap begins
    /// @param gap_length            the byte length of the gap
    /// @return true if the gap was filled, false if data arrived concurrently
    [[nodiscard]] inline bool try_fill_gap(
        concurrent::UnsafeBuffer& log_meta_data_buffer,
        concurrent::UnsafeBuffer& term_buffer,
        i32 term_id,
        i32 gap_offset,
        i32 gap_length)
    {
        // Validate gap_length: must be positive and FRAME_ALIGNMENT-aligned.
        if (gap_length <= 0 || gap_length % FrameDescriptor::FRAME_ALIGNMENT != 0)
        {
            return false;
        }

        // Walk backwards from the end of the gap, verifying all slots are empty.
        // If any slot has data, the gap has been filled by a concurrent writer.
        i32 offset = gap_offset + gap_length - FrameDescriptor::FRAME_ALIGNMENT;

        while (offset >= gap_offset)
        {
            if (term_buffer.get_i32_volatile(offset) != 0)
            {
                return false; // Data arrived — don't fill
            }
            offset -= FrameDescriptor::FRAME_ALIGNMENT;
        }

        // All slots are empty — fill the gap with a padding frame.
        // Step 1: Apply the default header (copies 32 bytes from metadata)
        LogBufferDescriptor::apply_default_header(log_meta_data_buffer, term_buffer, gap_offset);

        // Step 2: Set frame type to PAD
        FrameDescriptor::frame_type(term_buffer, gap_offset, protocol::HeaderFlyweight::HDR_TYPE_PAD);

        // Step 3: Write term_offset and term_id
        FrameDescriptor::frame_term_offset(term_buffer, gap_offset, gap_offset);
        FrameDescriptor::frame_term_id(term_buffer, gap_offset, term_id);

        // Step 4: Write frame_length with release semantics (publishes the padding frame)
        FrameDescriptor::frame_length_ordered(term_buffer, gap_offset, gap_length);

        return true;
    }
};

} // namespace caeron::logbuffer
