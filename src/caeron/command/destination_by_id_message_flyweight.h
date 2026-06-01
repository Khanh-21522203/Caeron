#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Control message for removing a destination by registration id for a
/// multi-destination-cast Publication or Subscription.
/// Extends CorrelatedMessageFlyweight in Java; standalone here.
/// Layout:
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i64  resource_registration_id
///   [24] i64  destination_registration_id
///   [32] total length = 32
inline constexpr i32 DESTINATION_BY_ID_MSG_LENGTH = 32;

class DestinationByIdMessageFlyweight {
public:
    static constexpr i32 CLIENT_ID_OFFSET                   = 0;
    static constexpr i32 CORRELATION_ID_OFFSET              = 8;
    static constexpr i32 RESOURCE_REGISTRATION_ID_OFFSET    = 16;
    static constexpr i32 DESTINATION_REGISTRATION_ID_OFFSET = 24;

    /// NOTE: offset_ is not bounds-checked. Callers must ensure offset_ >= 0
    /// and that the fixed fields (32 bytes) fit within the buffer capacity.
    explicit DestinationByIdMessageFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + CLIENT_ID_OFFSET); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + CLIENT_ID_OFFSET, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + CORRELATION_ID_OFFSET); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + CORRELATION_ID_OFFSET, value); }

    [[nodiscard]] i64 resource_registration_id() const noexcept { return buffer_.get_i64(offset_ + RESOURCE_REGISTRATION_ID_OFFSET); }
    void set_resource_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + RESOURCE_REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] i64 destination_registration_id() const noexcept { return buffer_.get_i64(offset_ + DESTINATION_REGISTRATION_ID_OFFSET); }
    void set_destination_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + DESTINATION_REGISTRATION_ID_OFFSET, value); }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
