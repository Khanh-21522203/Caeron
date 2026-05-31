#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"
#include "caeron/logbuffer/header.h"
#include "caeron/protocol/data_header_flyweight.h"

namespace caeron::logbuffer {

/// Reads frames from a term buffer and invokes a callback for each complete fragment.
///
/// This is the primary read path for subscribers. It performs a linear scan,
/// skipping padding frames, and delivers each fragment's payload to the handler.
namespace TermReader
{
    /// Read up to fragments_limit frames starting at term_offset.
    ///
    /// For each frame:
    ///   - Reads frame_length with volatile semantics
    ///   - If frame_length <= 0: stop (unpublished data)
    ///   - If padding frame (length < 0): skip, advance by aligned(-length)
    ///   - Otherwise: call handler with payload, advance by aligned(frame_length)
    ///
    /// @tparam FragmentHandler  callable(i32 offset, Header& header)
    /// @param term_buffer       the term buffer to read from
    /// @param term_offset       starting offset within the term
    /// @param handler           callback invoked for each fragment
    /// @param fragments_limit   maximum number of fragments to read
    /// @param header            header struct (reused across calls, fields updated per fragment)
    /// @return packed i64: (new_offset << 32) | fragments_read
    template<typename FragmentHandler>
    [[nodiscard]] inline i64 read(
        concurrent::UnsafeBuffer& term_buffer,
        i32 term_offset,
        FragmentHandler&& handler,
        i32 fragments_limit,
        Header& header)
    {
        const i32 capacity = term_buffer.capacity();
        i32 fragments_read = 0;

        while (fragments_read < fragments_limit && term_offset < capacity)
        {
            const i32 frame_length = FrameDescriptor::frame_length(term_buffer, term_offset);

            // frame_length == 0 means unpublished — stop scanning
            if (frame_length == 0)
            {
                break;
            }

            // frame_length < 0 means in-progress write — treat as end of data
            if (frame_length < 0)
            {
                break;
            }

            const i32 aligned_length = caeron::align(frame_length, FrameDescriptor::FRAME_ALIGNMENT);

            // Padding frame (type == PAD): skip it
            if (FrameDescriptor::is_padding_frame(term_buffer, term_offset))
            {
                term_offset += aligned_length;
                continue;
            }

            // Update header fields for this fragment
            header.offset = term_offset;
            header.term_id = FrameDescriptor::frame_term_id(term_buffer, term_offset);
            header.session_id = FrameDescriptor::frame_session_id(term_buffer, term_offset);
            header.stream_id = term_buffer.get_i32(term_offset + FrameDescriptor::STREAM_ID_FIELD_OFFSET);

            // Invoke the handler with the payload (past the header)
            handler(term_offset, header);

            ++fragments_read;
            term_offset += aligned_length;
        }

        return (static_cast<i64>(term_offset) << 32) | static_cast<i64>(fragments_read);
    }

    /// Extract the offset from a packed read outcome.
    [[nodiscard]] static constexpr i32 offset(i64 read_outcome) noexcept
    {
        return static_cast<i32>(static_cast<u64>(read_outcome) >> 32);
    }

    /// Extract the fragments read count from a packed read outcome.
    [[nodiscard]] static constexpr i32 fragments_read(i64 read_outcome) noexcept
    {
        return static_cast<i32>(read_outcome);
    }
};

} // namespace caeron::logbuffer
