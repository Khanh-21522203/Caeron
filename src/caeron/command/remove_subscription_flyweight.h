#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Control message for removing a Subscription.
/// Layout (matches Java Aeron RemoveSubscriptionFlyweight):
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i64  registration_id
///   [24] total length = 24
inline constexpr i32 REMOVE_SUBSCRIPTION_LENGTH = 24;

class RemoveSubscriptionFlyweight {
public:
    /// NOTE: offset_ is not bounds-checked. Callers must ensure offset_ >= 0
    /// and that the fixed fields (24 bytes) fit within the buffer capacity.
    explicit RemoveSubscriptionFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 client_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_client_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 8); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 8, value); }

    [[nodiscard]] i64 registration_id() const noexcept { return buffer_.get_i64(offset_ + 16); }
    void set_registration_id(i64 value) noexcept { buffer_.put_i64(offset_ + 16, value); }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
