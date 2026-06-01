#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <limits>
#include <span>
#include <vector>

namespace caeron::cnc {

/// Represents a mapped log buffer file with 3 term partitions + metadata.
struct LogBuffers
{
    static constexpr i32 PARTITION_COUNT = 3;

    /// The 3 term buffers.
    concurrent::UnsafeBuffer term_buffers[PARTITION_COUNT];

    /// The log metadata buffer.
    concurrent::UnsafeBuffer log_meta_data_buffer;

    /// Compute the byte offset of a term buffer within the log file.
    [[nodiscard]] static i64 compute_term_offset(i32 term_id, i32 initial_term_id, i64 term_length)
    {
        return (static_cast<i64>(term_id) - initial_term_id) % PARTITION_COUNT * term_length;
    }

    /// Compute the byte offset of the log metadata within the log file.
    [[nodiscard]] static i64 compute_log_meta_data_offset(i64 term_length)
    {
        if (term_length > 0 && term_length > std::numeric_limits<i64>::max() / PARTITION_COUNT)
            return -1;
        return term_length * PARTITION_COUNT;
    }

    /// Compute the total length of a log buffer file.
    [[nodiscard]] static i64 compute_log_length(i64 term_length, i32 page_size)
    {
        if (term_length > 0 && term_length > std::numeric_limits<i64>::max() / PARTITION_COUNT)
            return -1;
        const i64 partition_total = term_length * PARTITION_COUNT;
        if (page_size > 0 && partition_total > std::numeric_limits<i64>::max() - page_size)
            return -1;
        return partition_total + page_size;
    }
};

} // namespace caeron::cnc
