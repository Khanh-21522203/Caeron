#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"

namespace caeron::command {

/// Response to a create static counter request.
/// Layout:
///   [ 0] i64  correlation_id
///   [ 8] i32  counter_id
///   [12] total length = 12
inline constexpr i32 STATIC_COUNTER_LENGTH = 12;

class StaticCounterFlyweight {
public:
    /// NOTE: offset_ is not bounds-checked. Callers must ensure offset_ >= 0
    /// and that the fixed fields (12 bytes) fit within the buffer capacity.
    explicit StaticCounterFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset} {}

    [[nodiscard]] i64 correlation_id() const noexcept { return buffer_.get_i64(offset_ + 0); }
    void set_correlation_id(i64 value) noexcept { buffer_.put_i64(offset_ + 0, value); }

    [[nodiscard]] i32 counter_id() const noexcept { return buffer_.get_i32(offset_ + 8); }
    void set_counter_id(i32 value) noexcept { buffer_.put_i32(offset_ + 8, value); }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::command
