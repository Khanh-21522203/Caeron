#pragma once

#include "unsafe_buffer.h"

#include <concepts>

namespace caeron::concurrent {

/// Concept for buffer types that support atomic operations on mapped memory.
template <typename T>
concept AtomicBufferType = requires(T buf, i32 offset, i32 value, i64 lvalue) {
    { buf.get_i32_volatile(offset) } -> std::same_as<i32>;
    { buf.get_i64_volatile(offset) } -> std::same_as<i64>;
    { buf.get_i32_ordered(offset) } -> std::same_as<i32>;
    { buf.get_i64_ordered(offset) } -> std::same_as<i64>;
    buf.put_i32_volatile(offset, value);
    buf.put_i64_volatile(offset, lvalue);
    buf.put_i32_ordered(offset, value);
    buf.put_i64_ordered(offset, lvalue);
    { buf.compare_and_set_i32(offset, value, value) } -> std::same_as<bool>;
    { buf.compare_and_set_i64(offset, lvalue, lvalue) } -> std::same_as<bool>;
    { buf.get_and_add_i64(offset, lvalue) } -> std::same_as<i64>;
};

static_assert(AtomicBufferType<UnsafeBuffer>);

} // namespace caeron::concurrent
