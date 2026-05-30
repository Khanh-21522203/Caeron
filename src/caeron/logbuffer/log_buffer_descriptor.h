#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"

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

    // --- Helpers ---

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
};

} // namespace caeron::logbuffer
