#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/common/bit_util.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"

namespace caeron::logbuffer {

/// Scans for available contiguous data in a term buffer.
///
/// Used by the Sender agent to determine how many contiguous bytes can be
/// sent in a single network packet. Returns both the available byte count
/// and any padding encountered.
namespace TermScanner
{
    /// Scan for available contiguous data starting at the given offset.
    ///
    /// Greedily accumulates contiguous frames until:
    ///   - An unpublished frame is found (frame_length <= 0)
    ///   - A padding frame is found (after processing it)
    ///   - The accumulated bytes exceed max_length
    ///
    /// If the very first frame exceeds max_length, returns a negative available
    /// value to signal "data exists but cannot fit in one block."
    ///
    /// @param term_buffer  the term buffer to scan
    /// @param offset       starting offset within the term
    /// @param max_length   maximum bytes to collect
    /// @return packed i64: (padding << 32) | available
    ///         If available is negative, data exists but exceeds max_length.
    [[nodiscard]] inline i64 scan_for_availability(
        const concurrent::UnsafeBuffer& term_buffer,
        i32 offset,
        i32 max_length)
    {
        const i32 capacity = term_buffer.capacity();
        i32 available = 0;
        i32 padding = 0;

        while (offset < capacity)
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

            // Check if this is a padding frame (type == PAD)
            if (FrameDescriptor::is_padding_frame(term_buffer, offset))
            {
                // Padding frame: only the header counts toward available.
                // The body is reported as padding (dead bytes to skip).
                // Padding always terminates the scan.
                const i32 header_available = protocol::DataHeaderFlyweight::HEADER_LENGTH;

                if (available + header_available > max_length)
                {
                    // Even the header would exceed the limit
                    if (available == 0)
                    {
                        available = -header_available;
                    }
                    break;
                }

                available += header_available;
                padding = aligned_length - header_available;
                break;
            }

            if (available + aligned_length > max_length)
            {
                if (available == 0)
                {
                    // First frame exceeds max_length — signal with negative
                    available = -(available + aligned_length);
                }
                break;
            }

            available += aligned_length;
            offset += aligned_length;
        }

        return (static_cast<i64>(padding) << 32) | (static_cast<i64>(available) & 0xFFFFFFFFL);
    }

    /// Extract the padding value from a packed scan result.
    [[nodiscard]] static constexpr i32 padding(i64 scan_result) noexcept
    {
        return static_cast<i32>(static_cast<u64>(scan_result) >> 32);
    }

    /// Extract the available byte count from a packed scan result.
    [[nodiscard]] static constexpr i32 available(i64 scan_result) noexcept
    {
        return static_cast<i32>(scan_result);
    }
};

} // namespace caeron::logbuffer
