#pragma once

#include "unsafe_buffer.h"
#include "counters_manager.h"

#include <cassert>

namespace caeron::concurrent {

/// Atomic operations on a single counter value slot in shared memory.
///
/// Wraps a position in the values buffer managed by CountersManager.
/// All operations are atomic (ordered/volatile).
class AtomicCounter
{
public:
    AtomicCounter(i32 counter_id, UnsafeBuffer& values_buffer)
        : values_buffer_{values_buffer}
        , counter_id_{counter_id}
        , offset_{CountersManager::counter_offset(counter_id)}
    {
        assert(offset_ >= 0 && offset_ + CountersManager::COUNTER_LENGTH <= values_buffer_.capacity());
    }

    [[nodiscard]] i32 id() const noexcept { return counter_id_; }

    /// Atomic load (volatile).
    [[nodiscard]] i64 get() const noexcept
    {
        return values_buffer_.get_i64_volatile(offset_);
    }

    /// Atomic store (volatile).
    void set(i64 value) noexcept
    {
        values_buffer_.put_i64_volatile(offset_, value);
    }

    /// Atomic store (release ordered).
    void set_ordered(i64 value) noexcept
    {
        values_buffer_.put_i64_ordered(offset_, value);
    }

    /// Atomic load (acquire ordered).
    [[nodiscard]] i64 get_ordered() const noexcept
    {
        return values_buffer_.get_i64_ordered(offset_);
    }

    /// Atomic fetch_add. Returns the previous value.
    [[nodiscard]] i64 get_and_add(i64 increment) noexcept
    {
        return values_buffer_.get_and_add_i64(offset_, increment);
    }

    /// Atomic increment by 1. Returns the previous value.
    [[nodiscard]] i64 get_and_increment() noexcept
    {
        return get_and_add(1);
    }

    /// Atomic increment. Returns the value after the increment.
    [[nodiscard]] i64 increment_ordered(i64 increment) noexcept
    {
        return values_buffer_.get_and_add_i64(offset_, increment) + increment;
    }

    /// Atomic compare-and-set.
    [[nodiscard]] bool compare_and_set(i64 expected, i64 desired) noexcept
    {
        return values_buffer_.compare_and_set_i64(offset_, expected, desired);
    }

private:
    UnsafeBuffer& values_buffer_;
    i32 counter_id_;
    i32 offset_;
};

} // namespace caeron::concurrent
