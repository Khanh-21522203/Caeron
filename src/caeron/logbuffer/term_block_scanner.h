#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"

namespace caeron::logbuffer {

/// Scans for a contiguous block of complete fragments for batch processing.
///
/// Used by the Sender to collect multiple frames into a single network block.
/// Unlike TermScanner which returns byte counts, BlockScanner returns the end
/// offset of the contiguous block.
namespace TermBlockScanner
{
    /// Scan for a contiguous block of complete frames.
    ///
    /// Terminates when:
    ///   - frame_length <= 0 (end of data)
    ///   - A padding frame is encountered (padding at start is consumed;
    ///     padding mid-block terminates the block before the padding)
    ///   - The next frame would exceed limit_offset
    ///
    /// @param term_buffer   the term buffer to scan
    /// @param term_offset   starting offset
    /// @param limit_offset  upper bound (don't scan past this)
    /// @return the offset past the last frame in the block
    [[nodiscard]] inline i32 scan(
        const concurrent::UnsafeBuffer& term_buffer,
        i32 term_offset,
        i32 limit_offset)
    {
        i32 offset = term_offset;
        const i32 capacity = term_buffer.capacity();

        while (offset < capacity && offset < limit_offset)
        {
            const i32 frame_length = FrameDescriptor::frame_length(term_buffer, offset);

            // frame_length == 0 means unpublished — stop
            if (frame_length == 0)
            {
                break;
            }

            // frame_length < 0 means in-progress write — stop
            if (frame_length < 0)
            {
                break;
            }

            const i32 aligned_length = caeron::align(frame_length, FrameDescriptor::FRAME_ALIGNMENT);

            // Check for padding frame (type == PAD)
            if (FrameDescriptor::is_padding_frame(term_buffer, offset))
            {
                if (offset == term_offset)
                {
                    // Padding at the start: only the header needs to be delivered.
                    // Return past the header only, not the full aligned padding length.
                    return offset + protocol::DataHeaderFlyweight::HEADER_LENGTH;
                }
                // Padding mid-block: terminate before the padding
                break;
            }

            // Check if the next frame would exceed the limit
            if (offset + aligned_length > limit_offset)
            {
                break;
            }

            offset += aligned_length;
        }

        return offset;
    }
};

} // namespace caeron::logbuffer
