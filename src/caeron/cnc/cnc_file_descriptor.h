#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::cnc {

/// Layout of the CnC (Command and Control) memory-mapped file shared between
/// the driver process and client processes.
///
/// +-----------------------------+
/// |        Meta Data            |  (128 bytes)
/// +-----------------------------+
/// |    to-driver Buffer         |  (ManyToOneRingBuffer)
/// +-----------------------------+
/// |    to-clients Buffer        |  (BroadcastTransmitter)
/// +-----------------------------+
/// | Counters Metadata Buffer    |
/// +-----------------------------+
/// |  Counters Values Buffer     |
/// +-----------------------------+
/// |        Error Log            |
/// +-----------------------------+
struct CncFileDescriptor
{
    // Meta data field offsets (within the first 128 bytes)
    static constexpr i32 CNC_VERSION_FIELD_OFFSET = 0;
    static constexpr i32 TO_DRIVER_BUFFER_LENGTH_FIELD_OFFSET = 4;
    static constexpr i32 TO_CLIENTS_BUFFER_LENGTH_FIELD_OFFSET = 8;
    static constexpr i32 COUNTERS_METADATA_BUFFER_LENGTH_FIELD_OFFSET = 12;
    static constexpr i32 COUNTERS_VALUES_BUFFER_LENGTH_FIELD_OFFSET = 16;
    static constexpr i32 ERROR_LOG_BUFFER_LENGTH_FIELD_OFFSET = 20;
    static constexpr i32 CLIENT_LIVENESS_TIMEOUT_FIELD_OFFSET = 24; // i64
    static constexpr i32 PID_FIELD_OFFSET = 32; // i64
    static constexpr i32 START_TIMESTAMP_FIELD_OFFSET = 40; // i64
    static constexpr i32 META_DATA_LENGTH = 128;

    // Current CnC version
    static constexpr i32 CNC_VERSION = 15;

    // Pre-computed section offsets (these depend on configured buffer sizes)
    static constexpr i32 to_driver_buffer_offset(i32 /*meta_data_length*/ = META_DATA_LENGTH)
    {
        return META_DATA_LENGTH;
    }

    static i32 to_clients_buffer_offset(i32 to_driver_buffer_length)
    {
        return to_driver_buffer_offset() + to_driver_buffer_length;
    }

    static i32 counters_metadata_buffer_offset(i32 to_driver_buffer_length,
                                                i32 to_clients_buffer_length)
    {
        return to_clients_buffer_offset(to_driver_buffer_length) + to_clients_buffer_length;
    }

    static i32 counters_values_buffer_offset(i32 to_driver_buffer_length,
                                              i32 to_clients_buffer_length,
                                              i32 counters_metadata_buffer_length)
    {
        return counters_metadata_buffer_offset(to_driver_buffer_length,
                                               to_clients_buffer_length) +
               counters_metadata_buffer_length;
    }

    static i32 error_log_buffer_offset(i32 to_driver_buffer_length,
                                        i32 to_clients_buffer_length,
                                        i32 counters_metadata_buffer_length,
                                        i32 counters_values_buffer_length)
    {
        return counters_values_buffer_offset(to_driver_buffer_length,
                                             to_clients_buffer_length,
                                             counters_metadata_buffer_length) +
               counters_values_buffer_length;
    }

    static i32 total_length(i32 to_driver_buffer_length,
                            i32 to_clients_buffer_length,
                            i32 counters_metadata_buffer_length,
                            i32 counters_values_buffer_length,
                            i32 error_log_buffer_length)
    {
        return error_log_buffer_offset(to_driver_buffer_length,
                                       to_clients_buffer_length,
                                       counters_metadata_buffer_length,
                                       counters_values_buffer_length) +
               error_log_buffer_length;
    }

    /// Fill in the meta data header fields.
    static void fill_meta_data(concurrent::UnsafeBuffer& cnc_buffer,
                               i32 to_driver_buffer_length,
                               i32 to_clients_buffer_length,
                               i32 counters_metadata_buffer_length,
                               i32 counters_values_buffer_length,
                               i32 error_log_buffer_length,
                               i64 client_liveness_timeout_ns,
                               i64 pid,
                               i64 start_timestamp)
    {
        cnc_buffer.put_i32(CNC_VERSION_FIELD_OFFSET, CNC_VERSION);
        cnc_buffer.put_i32(TO_DRIVER_BUFFER_LENGTH_FIELD_OFFSET, to_driver_buffer_length);
        cnc_buffer.put_i32(TO_CLIENTS_BUFFER_LENGTH_FIELD_OFFSET, to_clients_buffer_length);
        cnc_buffer.put_i32(COUNTERS_METADATA_BUFFER_LENGTH_FIELD_OFFSET, counters_metadata_buffer_length);
        cnc_buffer.put_i32(COUNTERS_VALUES_BUFFER_LENGTH_FIELD_OFFSET, counters_values_buffer_length);
        cnc_buffer.put_i32(ERROR_LOG_BUFFER_LENGTH_FIELD_OFFSET, error_log_buffer_length);
        cnc_buffer.put_i64(CLIENT_LIVENESS_TIMEOUT_FIELD_OFFSET, client_liveness_timeout_ns);
        cnc_buffer.put_i64(PID_FIELD_OFFSET, pid);
        cnc_buffer.put_i64(START_TIMESTAMP_FIELD_OFFSET, start_timestamp);
    }

    /// Read the CnC version from the buffer. Returns 0 if buffer is too small.
    [[nodiscard]] static i32 cnc_version(concurrent::UnsafeBuffer& cnc_buffer)
    {
        if (cnc_buffer.capacity() < CNC_VERSION_FIELD_OFFSET + 4)
            return 0;
        return cnc_buffer.get_i32(CNC_VERSION_FIELD_OFFSET);
    }

    [[nodiscard]] static i32 to_driver_buffer_length(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i32(TO_DRIVER_BUFFER_LENGTH_FIELD_OFFSET);
    }

    [[nodiscard]] static i32 to_clients_buffer_length(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i32(TO_CLIENTS_BUFFER_LENGTH_FIELD_OFFSET);
    }

    [[nodiscard]] static i32 counters_metadata_buffer_length(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i32(COUNTERS_METADATA_BUFFER_LENGTH_FIELD_OFFSET);
    }

    [[nodiscard]] static i32 counters_values_buffer_length(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i32(COUNTERS_VALUES_BUFFER_LENGTH_FIELD_OFFSET);
    }

    [[nodiscard]] static i32 error_log_buffer_length(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i32(ERROR_LOG_BUFFER_LENGTH_FIELD_OFFSET);
    }

    [[nodiscard]] static i64 client_liveness_timeout(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i64(CLIENT_LIVENESS_TIMEOUT_FIELD_OFFSET);
    }

    [[nodiscard]] static i64 pid(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i64(PID_FIELD_OFFSET);
    }

    [[nodiscard]] static i64 start_timestamp(concurrent::UnsafeBuffer& cnc_buffer)
    {
        return cnc_buffer.get_i64(START_TIMESTAMP_FIELD_OFFSET);
    }
};

} // namespace caeron::cnc
