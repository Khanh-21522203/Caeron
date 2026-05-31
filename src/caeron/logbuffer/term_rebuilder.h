#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"
#include "caeron/protocol/data_header_flyweight.h"

namespace caeron::logbuffer {

/// Inserts received out-of-order frames into the correct position in a term buffer.
///
/// When the network delivers frames out of order, the receiver uses TermRebuilder
/// to place each frame at its correct offset. The insertion is idempotent — if the
/// slot already contains a frame, the write is skipped (duplicate detection).
namespace TermRebuilder
{
    /// Insert a received packet into the term buffer at the given offset.
    ///
    /// Only writes if the slot is empty (frame_length == 0). This makes the
    /// operation idempotent for duplicate packets.
    ///
    /// The write order is:
    ///   1. Copy payload (bytes from HEADER_LENGTH onward)
    ///   2. Write header fields (term_id, stream_id, session_id)
    ///   3. Write frame_length LAST with release semantics (publication barrier)
    ///
    /// @param term_buffer  the term buffer to write into
    /// @param term_offset  the offset within the term buffer for this frame
    /// @param packet       the received packet buffer (full frame including header)
    /// @param length       total length of the packet
    inline void insert(
        concurrent::UnsafeBuffer& term_buffer,
        i32 term_offset,
        concurrent::UnsafeBuffer& packet,
        i32 length)
    {
        // Check if the slot is empty (not yet written).
        // If non-zero, a previous insert already placed data here — skip.
        if (term_buffer.get_i32_volatile(term_offset) != 0)
        {
            return;
        }

        const i32 header_length = protocol::DataHeaderFlyweight::HEADER_LENGTH;
        const i32 payload_length = length - header_length;

        // Step 1: Copy the payload first (plain writes).
        if (payload_length > 0)
        {
            term_buffer.put_bytes(
                term_offset + header_length,
                packet.data() + header_length,
                payload_length);
        }

        // Step 2: Write header fields in reverse order.
        // The frame layout: [length:4][version:1][flags:1][type:2][term_offset:4]
        //                   [session_id:4][stream_id:4][term_id:4][reserved:8]
        // We write term_id first, then stream_id, session_id, etc.
        term_buffer.put_i64(term_offset + 24, packet.get_i64(24));  // reserved value
        term_buffer.put_i64(term_offset + 16, packet.get_i64(16));  // stream_id + term_id
        term_buffer.put_i64(term_offset + 8, packet.get_i64(8));    // term_offset + session_id
        term_buffer.put_i32(term_offset + 4, packet.get_i32(4));    // version + flags + type

        // Step 3: Write frame_length LAST with release semantics.
        // The frame_length comes from the packet header (not the total packet length),
        // since a packet may contain multiple frames.
        // This is the publication barrier — any thread that reads frame_length > 0
        // with acquire semantics will see all the writes above.
        term_buffer.put_i32_ordered(term_offset, packet.get_i32(0));
    }
};

} // namespace caeron::logbuffer
