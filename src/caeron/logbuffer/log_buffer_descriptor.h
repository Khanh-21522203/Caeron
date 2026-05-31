#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/protocol/data_header_flyweight.h"

#include <cstring>

namespace caeron::logbuffer {

/// Describes the layout of Aeron's tri-buffer log structure.
///
/// The log is composed of three term buffers and one metadata buffer.
/// Tail counters, term IDs, and other bookkeeping live in the metadata region.
struct LogBufferDescriptor
{
    static constexpr i32 PARTITION_COUNT = 3;

    static constexpr i32 TERM_MIN_LENGTH = 64 * 1024;              // 64 KB
    static constexpr i32 TERM_MAX_LENGTH = 1024 * 1024 * 1024;     // 1 GB
    static constexpr i32 PAGE_MIN_SIZE   = 4 * 1024;               // 4 KB
    static constexpr i32 PAGE_MAX_SIZE   = 1024 * 1024 * 1024;     // 1 GB

    static constexpr i32 LOG_META_DATA_LENGTH = PAGE_MIN_SIZE;     // 4 KB
    static constexpr i32 FRAME_ALIGNMENT      = 32;

    // --- Tail counter offsets (one i64 per partition) ---

    static constexpr i32 TAIL_COUNTER_OFFSET_0 = 0;
    static constexpr i32 TAIL_COUNTER_OFFSET_1 = 8;
    static constexpr i32 TAIL_COUNTER_OFFSET_2 = 16;

    /// Active term count (i32).
    static constexpr i32 ACTIVE_TERM_COUNT_OFFSET = 24;

    // Cache line padding up to offset 128.

    /// End-of-stream position (i64).
    static constexpr i32 END_OF_STREAM_POSITION_OFFSET = 128;

    /// Whether the stream is connected (i32).
    static constexpr i32 IS_CONNECTED_OFFSET = 136;

    /// Active transport count (i32).
    static constexpr i32 ACTIVE_TRANSPORT_COUNT_OFFSET = 140;

    // Cache line padding up to offset 256.

    /// Log metadata section starts at offset 256.
    static constexpr i32 LOG_META_DATA_SECTION_OFFSET = 256;

    /// Registration / correlation ID (i64).
    static constexpr i32 LOG_META_DATA_SECTION_REGISTRATION_ID_OFFSET = 256;

    /// Initial term ID (i32).
    static constexpr i32 INITIAL_TERM_ID_OFFSET = 264;

    /// Default frame header length (i32).
    static constexpr i32 DEFAULT_FRAME_HEADER_LENGTH_OFFSET = 268;

    /// MTU length (i32).
    static constexpr i32 MTU_LENGTH_OFFSET = 272;

    /// Term length (i32).
    static constexpr i32 TERM_LENGTH_OFFSET = 276;

    /// Page size (i32).
    static constexpr i32 PAGE_SIZE_OFFSET = 280;

    /// Publication window length (i32).
    static constexpr i32 PUBLICATION_WINDOW_LENGTH_OFFSET = 284;

    /// Receiver window length (i32).
    static constexpr i32 RECEIVER_WINDOW_LENGTH_OFFSET = 288;

    /// Socket send buffer length (i32).
    static constexpr i32 SOCKET_SNDBUF_LENGTH_OFFSET = 292;

    /// OS default send buffer length (i32).
    static constexpr i32 OS_DEFAULT_SNDBUF_LENGTH_OFFSET = 296;

    /// OS max send buffer length (i32).
    static constexpr i32 OS_MAX_SNDBUF_LENGTH_OFFSET = 300;

    /// Socket receive buffer length (i32).
    static constexpr i32 SOCKET_RCVBUF_LENGTH_OFFSET = 304;

    /// OS default receive buffer length (i32).
    static constexpr i32 OS_DEFAULT_RCVBUF_LENGTH_OFFSET = 308;

    /// OS max receive buffer length (i32).
    static constexpr i32 OS_MAX_RCVBUF_LENGTH_OFFSET = 312;

    /// Max resend (i32).
    static constexpr i32 MAX_RESEND_OFFSET = 316;

    /// Default frame header (128 bytes starting at offset 320).
    static constexpr i32 DEFAULT_FRAME_HEADER_OFFSET = 320;

    /// Maximum length of a default frame header.
    static constexpr i32 DEFAULT_FRAME_HEADER_MAX_LENGTH = 128;

    /// Entity tag (i64).
    static constexpr i32 ENTITY_TAG_OFFSET = 448;

    /// Response correlation ID (i64).
    static constexpr i32 RESPONSE_CORRELATION_ID_OFFSET = 456;

    /// Linger timeout (i64).
    static constexpr i32 LINGER_TIMEOUT_OFFSET = 464;

    /// Untethered window limit timeout (i64).
    static constexpr i32 UNTETHERED_WINDOW_LIMIT_TIMEOUT_OFFSET = 472;

    /// Untethered resting timeout (i64).
    static constexpr i32 UNTETHERED_RESTING_TIMEOUT_OFFSET = 480;

    // --- Packing helpers ---

    /// Pack a term offset and term ID into a single i64 tail counter.
    /// The term offset occupies the upper 32 bits, term ID the lower 32 bits.
    [[nodiscard]] static constexpr i64 pack_tail(i32 term_offset, i32 term_id) noexcept
    {
        return (static_cast<i64>(term_offset) << 32) | (static_cast<i64>(term_id) & 0xFFFFFFFFL);
    }

    /// Extract the term ID from a packed tail counter (lower 32 bits).
    [[nodiscard]] static constexpr i32 tail_term_id(i64 tail) noexcept
    {
        return static_cast<i32>(tail);
    }

    /// Extract the term offset from a packed tail counter (upper 32 bits).
    [[nodiscard]] static constexpr i32 tail_term_offset(i64 tail) noexcept
    {
        return static_cast<i32>(static_cast<u64>(tail) >> 32);
    }

    /// Compute the partition index from the initial term ID and active term ID.
    [[nodiscard]] static constexpr i32 index_by_term(i32 initial_term_id, i32 active_term_id) noexcept
    {
        return (active_term_id - initial_term_id) % PARTITION_COUNT;
    }

    /// Compute the partition index from a raw term count.
    [[nodiscard]] static constexpr i32 index_by_term_count(i32 term_count) noexcept
    {
        return term_count % PARTITION_COUNT;
    }

    /// Compute an absolute position from the active term ID, term offset,
    /// position bits to shift, and initial term ID.
    [[nodiscard]] static constexpr i64 compute_position(
        i32 active_term_id,
        i32 term_offset,
        i32 position_bits_to_shift,
        i32 initial_term_id) noexcept
    {
        const i64 term_count = static_cast<i64>(active_term_id - initial_term_id);
        return (term_count << position_bits_to_shift) + term_offset;
    }

    /// Compute the number of bits to shift for a given term length (log2 of term_length).
    [[nodiscard]] static constexpr i32 position_bits_to_shift(i32 term_length) noexcept
    {
        return static_cast<i32>(std::countr_zero(static_cast<u32>(term_length)));
    }

    // --- Metadata accessors ---

    /// Read the active term count with acquire semantics.
    /// Pairs with the release store in rotate_log to ensure the reader sees
    /// the updated tail counter for the new term.
    [[nodiscard]] static i32 active_term_count(concurrent::UnsafeBuffer& metadata_buffer) noexcept
    {
        return metadata_buffer.get_i32_ordered(ACTIVE_TERM_COUNT_OFFSET);
    }

    /// Read the packed tail counter for a partition with acquire semantics.
    /// Pairs with the release store in rotate_log and tail counter updates.
    [[nodiscard]] static i64 raw_tail_volatile(concurrent::UnsafeBuffer& metadata_buffer, i32 partition_index) noexcept
    {
        return metadata_buffer.get_i64_ordered(TAIL_COUNTER_OFFSET_0 + (sizeof(i64) * partition_index));
    }

    /// Copy the default 32-byte frame header from metadata into a term buffer at the given offset.
    /// This is used by TermGapFiller and TermUnblocker to initialize frame headers.
    static void apply_default_header(
        concurrent::UnsafeBuffer& metadata_buffer,
        concurrent::UnsafeBuffer& term_buffer,
        i32 term_offset) noexcept
    {
        term_buffer.put_bytes(
            term_offset,
            metadata_buffer.data() + DEFAULT_FRAME_HEADER_OFFSET,
            protocol::DataHeaderFlyweight::HEADER_LENGTH);
    }

    /// Rotate the log to the next term partition. Uses CAS to ensure only one
    /// thread performs the rotation. Safe for concurrent use.
    ///
    /// @param metadata_buffer  the log metadata buffer
    /// @param term_count       the current active term count
    /// @param term_id          the current term ID
    /// @return true if the log was rotated by this call
    static bool rotate_log(
        concurrent::UnsafeBuffer& metadata_buffer,
        i32 term_count,
        i32 term_id) noexcept
    {
        const i32 next_term_id = term_id + 1;
        const i32 next_term_count = term_count + 1;
        const i32 next_index = index_by_term_count(next_term_count);
        const i32 expected_term_id = next_term_id - PARTITION_COUNT;

        // CAS loop: update the next partition's tail counter.
        // Only proceed if the tail's term_id matches what we expect
        // (i.e., the partition is ready to be reused).
        i64 raw_tail;
        bool tail_updated = false;
        do
        {
            raw_tail = raw_tail_volatile(metadata_buffer, next_index);
            if (expected_term_id != tail_term_id(raw_tail))
            {
                // Tail doesn't match — partition is not ready.
                // Another thread may have already rotated, or metadata is stale.
                return false;
            }
            if (metadata_buffer.compare_and_set_i64(
                TAIL_COUNTER_OFFSET_0 + (sizeof(i64) * next_index),
                raw_tail,
                pack_tail(0, next_term_id)))
            {
                tail_updated = true;
                break;
            }
        }
        while (true);

        // CAS: advance active_term_count only if we updated the tail
        if (tail_updated)
        {
            return metadata_buffer.compare_and_set_i32(
                ACTIVE_TERM_COUNT_OFFSET, term_count, next_term_count);
        }

        return false;
    }

    /// Initialize the tail counter for a partition with a given term ID.
    static void initialise_tail_with_term_id(
        concurrent::UnsafeBuffer& metadata_buffer,
        i32 partition_index,
        i32 term_id) noexcept
    {
        metadata_buffer.put_i64(
            TAIL_COUNTER_OFFSET_0 + (partition_index * sizeof(i64)),
            pack_tail(0, term_id));
    }
};

} // namespace caeron::logbuffer
