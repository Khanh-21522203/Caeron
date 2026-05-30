#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

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
        return static_cast<i64>(term_id - initial_term_id) % PARTITION_COUNT * term_length;
    }

    /// Compute the byte offset of the log metadata within the log file.
    [[nodiscard]] static i64 compute_log_meta_data_offset(i64 term_length)
    {
        return term_length * PARTITION_COUNT;
    }

    /// Compute the total length of a log buffer file.
    [[nodiscard]] static i64 compute_log_length(i64 term_length, i32 page_size)
    {
        i64 log_length = term_length * PARTITION_COUNT + page_size;
        return log_length;
    }
};

} // namespace caeron::cnc
