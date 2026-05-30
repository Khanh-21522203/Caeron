#pragma once

#include "unsafe_buffer.h"
#include "counters_manager.h"

#include <concepts>

namespace caeron::concurrent {

/// Concept for position counters. A position represents a monotonically
/// increasing byte offset in a stream or log.
template <typename T>
concept PositionType = requires(T pos, i64 value) {
    { pos.get() } -> std::convertible_to<i64>;
    { pos.get_volatile() } -> std::convertible_to<i64>;
    { pos.get_ordered() } -> std::convertible_to<i64>;
    pos.set(value);
    pos.set_ordered(value);
    { pos.get_and_add(value) } -> std::convertible_to<i64>;
    { pos.increment_ordered(value) } -> std::convertible_to<i64>;
};

/// Position implementation backed by an UnsafeBuffer slot.
///
/// Each position occupies a single i64 slot at a given offset in the buffer.
/// The offset is typically counter_id * COUNTER_LENGTH.
class UnsafeBufferPosition
{
public:
    UnsafeBufferPosition(UnsafeBuffer& buffer, i32 offset) noexcept
        : buffer_{buffer}
        , offset_{offset}
    {}

    /// Construct from a counter_id using CountersManager's offset calculation.
    UnsafeBufferPosition(UnsafeBuffer& buffer, i32 counter_id, i32 /*counter_length*/) noexcept
        : buffer_{buffer}
        , offset_{CountersManager::counter_offset(counter_id)}
    {}

    [[nodiscard]] i64 get() const noexcept
    {
        return buffer_.get_i64_volatile(offset_);
    }

    [[nodiscard]] i64 get_volatile() const noexcept
    {
        return buffer_.get_i64_volatile(offset_);
    }

    [[nodiscard]] i64 get_ordered() const noexcept
    {
        return buffer_.get_i64_ordered(offset_);
    }

    void set(i64 value) noexcept
    {
        buffer_.put_i64_volatile(offset_, value);
    }

    void set_ordered(i64 value) noexcept
    {
        buffer_.put_i64_ordered(offset_, value);
    }

    [[nodiscard]] i64 get_and_add(i64 increment) noexcept
    {
        return buffer_.get_and_add_i64(offset_, increment);
    }

    [[nodiscard]] i64 increment_ordered(i64 increment) noexcept
    {
        return buffer_.get_and_add_i64(offset_, increment) + increment;
    }

private:
    UnsafeBuffer& buffer_;
    i32 offset_;
};

static_assert(PositionType<UnsafeBufferPosition>);

} // namespace caeron::concurrent
