#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/common/bit_util.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"

namespace caeron::logbuffer {

/// Detects gaps in received data for NAK (Negative Acknowledgment) generation.
///
/// When frames arrive out of order, the receiver needs to know which frames
/// are missing so it can request retransmission. TermGapScanner performs a
/// two-phase scan: first finding the end of contiguous received data, then
/// measuring the extent of the gap.
struct GapHandler
{
    virtual ~GapHandler() = default;

    /// Called when a gap is detected.
    /// @param term_id  the term in which the gap was found
    /// @param offset   the byte offset of the gap start
    /// @param length   the byte length of the gap
    virtual void on_gap(i32 term_id, i32 offset, i32 length) = 0;
};

/// Scans for gaps in received term buffer data.
namespace TermGapScanner
{
    /// Scan for a gap starting from term_offset up to limit_offset.
    ///
    /// Phase 1: Walk contiguous frames (frame_length > 0) advancing by aligned length.
    ///          Stop at a zero-length frame or limit_offset.
    /// Phase 2: From the gap start, walk forward in HEADER_LENGTH steps looking for
    ///          the next non-zero frame. The gap extends from gap_start to that point.
    ///
    /// @param term_buffer   the term buffer to scan
    /// @param term_id       the term ID (passed to the gap handler)
    /// @param term_offset   starting offset to scan from
    /// @param limit_offset  upper bound offset (don't scan past this)
    /// @param handler       callback invoked when a gap is found
    /// @return the offset of the last contiguous frame found
    [[nodiscard]] inline i32 scan_for_gap(
        const concurrent::UnsafeBuffer& term_buffer,
        i32 term_id,
        i32 term_offset,
        i32 limit_offset,
        GapHandler& handler)
    {
        i32 offset = term_offset;

        // Phase 1: Walk contiguous frames
        while (offset < limit_offset)
        {
            const i32 frame_length = FrameDescriptor::frame_length(term_buffer, offset);
            if (frame_length == 0)
            {
                break;
            }
            if (frame_length < 0)
            {
                // In-progress write — treat as gap boundary
                break;
            }
            offset += caeron::align(frame_length, FrameDescriptor::FRAME_ALIGNMENT);
        }

        // If we stopped before limit_offset, there's a gap
        if (offset < limit_offset)
        {
            const i32 gap_begin = offset;

            // Phase 2: Walk forward by HEADER_LENGTH steps to find the next non-zero frame
            while (offset < limit_offset)
            {
                if (FrameDescriptor::frame_length(term_buffer, offset) != 0)
                {
                    break;
                }
                offset += protocol::DataHeaderFlyweight::HEADER_LENGTH;
            }

            const i32 gap_length = offset - gap_begin;
            if (gap_length > 0)
            {
                handler.on_gap(term_id, gap_begin, gap_length);
            }
        }

        return offset;
    }
};

} // namespace caeron::logbuffer
