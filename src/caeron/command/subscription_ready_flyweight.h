#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i32  channel_status_counter_id
///   [12] total length = 12
inline constexpr i32 SUBSCRIPTION_READY_LENGTH = 12;

class SubscriptionReadyFlyweight {
public:
    explicit SubscriptionReadyFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 channel_status_counter_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    void set_channel_status_counter_id(i32 value) noexcept { buffer_.put_i32(offset_ + 8, value); }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
