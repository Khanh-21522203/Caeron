#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Control message for getting next available session id from the media driver.
/// Extends CorrelatedMessageFlyweight in Java; standalone here.
/// Layout:
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i32  stream_id
///   [20] total length = 20
inline constexpr i32 GET_NEXT_SESSION_ID_MSG_LENGTH = 20;

class GetNextAvailableSessionIdMessageFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET      = 0;
    static constexpr i32 CORRELATION_ID_OFFSET = 8;
    static constexpr i32 STREAM_ID_OFFSET      = 16;

    /// NOTE: offset_ is not bounds-checked. Callers must ensure offset_ >= 0
    /// and that the fixed fields (20 bytes) fit within the buffer capacity.
    explicit GetNextAvailableSessionIdMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i32 stream_id() const noexcept { return buffer_.get_i32(offset_ + STREAM_ID_OFFSET); }
    void set_stream_id(i32 value) noexcept { buffer_.put_i32(offset_ + STREAM_ID_OFFSET, value); }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
