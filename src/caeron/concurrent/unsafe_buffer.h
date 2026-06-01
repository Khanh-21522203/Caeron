#pragma once

#include "caeron/common/types.h"

#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <span>

namespace caeron::concurrent {

/// Non-owning typed view over a raw byte span. Provides plain, volatile, and
/// ordered (store-release / load-acquire) read/write accessors. All multi-byte
/// accessors use little-endian byte order.
///
/// NOTE: Atomic operations reinterpret_cast raw bytes to std::atomic<T>*. This is
/// technically undefined behavior per the C++ standard (strict aliasing, object lifetime).
/// It works in practice on all major compilers (GCC, Clang, MSVC) when:
///   1. The underlying storage is properly aligned (debug asserts added).
///   2. The storage is backed by char/byte arrays (common implementation guarantee).
/// C++23 std::start_lifetime_as<> could make this formally correct but is not yet
/// widely supported. This mirrors Java Aeron's use of sun.misc.Unsafe for direct
/// memory atomic access.
///
/// SAFETY CONTRACT: All bounds and alignment checks use `assert()` which compiles
/// away in release builds (NDEBUG). Callers MUST ensure:
///   - offsets are non-negative and within [0, capacity)
///   - multi-byte accessors are naturally aligned (offset % sizeof(T) == 0)
///   - the buffer lifetime exceeds all accessor calls
/// Violating these contracts in release mode causes undefined behavior (out-of-bounds
/// access, misaligned atomics). This is a deliberate performance trade-off matching
/// Java Aeron's Unsafe-based design -- the hot path must not branch on every access.
class UnsafeBuffer
{
public:
    UnsafeBuffer() noexcept = default;

    UnsafeBuffer(std::span<std::byte> buffer) noexcept
        : data_{buffer.data()}, capacity_{static_cast<i32>(buffer.size())}
    {}

    UnsafeBuffer(void* data, i32 capacity) noexcept
        : data_{static_cast<std::byte*>(data)}, capacity_{capacity}
    {}

    [[nodiscard]] std::byte* data() noexcept { return data_; }
    [[nodiscard]] const std::byte* data() const noexcept { return data_; }
    [[nodiscard]] i32 capacity() const noexcept { return capacity_; }

    // --- Plain accessors ---

    [[nodiscard]] u8 get_u8(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset < capacity_);
        u8 val;
        std::memcpy(&val, data_ + offset, sizeof(u8));
        return val;
    }

    void put_u8(i32 offset, u8 value) noexcept
    {
        assert(offset >= 0 && offset < capacity_);
        std::memcpy(data_ + offset, &value, sizeof(u8));
    }

    [[nodiscard]] i8 get_i8(i32 offset) const noexcept
    {
        return static_cast<i8>(get_u8(offset));
    }

    void put_i8(i32 offset, i8 value) noexcept
    {
        put_u8(offset, static_cast<u8>(value));
    }

    [[nodiscard]] u16 get_u16(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 2 <= capacity_);
        u16 val;
        std::memcpy(&val, data_ + offset, sizeof(u16));
        return val; // native LE
    }

    void put_u16(i32 offset, u16 value) noexcept
    {
        assert(offset >= 0 && offset + 2 <= capacity_);
        std::memcpy(data_ + offset, &value, sizeof(u16));
    }

    [[nodiscard]] i16 get_i16(i32 offset) const noexcept
    {
        return static_cast<i16>(get_u16(offset));
    }

    void put_i16(i32 offset, i16 value) noexcept
    {
        put_u16(offset, static_cast<u16>(value));
    }

    [[nodiscard]] u32 get_u32(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        u32 val;
        std::memcpy(&val, data_ + offset, sizeof(u32));
        return val;
    }

    void put_u32(i32 offset, u32 value) noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        std::memcpy(data_ + offset, &value, sizeof(u32));
    }

    [[nodiscard]] i32 get_i32(i32 offset) const noexcept
    {
        return static_cast<i32>(get_u32(offset));
    }

    void put_i32(i32 offset, i32 value) noexcept
    {
        put_u32(offset, static_cast<u32>(value));
    }

    [[nodiscard]] u64 get_u64(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        u64 val;
        std::memcpy(&val, data_ + offset, sizeof(u64));
        return val;
    }

    void put_u64(i32 offset, u64 value) noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        std::memcpy(data_ + offset, &value, sizeof(u64));
    }

    [[nodiscard]] i64 get_i64(i32 offset) const noexcept
    {
        return static_cast<i64>(get_u64(offset));
    }

    void put_i64(i32 offset, i64 value) noexcept
    {
        put_u64(offset, static_cast<u64>(value));
    }

    // --- Volatile accessors (atomic load/store with relaxed ordering) ---

    [[nodiscard]] i32 get_i32_volatile(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<const std::atomic<i32>*>(data_ + offset);
        return ptr->load(std::memory_order_relaxed);
    }

    void put_i32_volatile(i32 offset, i32 value) noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i32>*>(data_ + offset);
        ptr->store(value, std::memory_order_relaxed);
    }

    [[nodiscard]] i64 get_i64_volatile(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<const std::atomic<i64>*>(data_ + offset);
        return ptr->load(std::memory_order_relaxed);
    }

    void put_i64_volatile(i32 offset, i64 value) noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i64>*>(data_ + offset);
        ptr->store(value, std::memory_order_relaxed);
    }

    // --- Ordered accessors (store-release / load-acquire) ---

    [[nodiscard]] i32 get_i32_ordered(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<const std::atomic<i32>*>(data_ + offset);
        return ptr->load(std::memory_order_acquire);
    }

    void put_i32_ordered(i32 offset, i32 value) noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i32>*>(data_ + offset);
        ptr->store(value, std::memory_order_release);
    }

    [[nodiscard]] i64 get_i64_ordered(i32 offset) const noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<const std::atomic<i64>*>(data_ + offset);
        return ptr->load(std::memory_order_acquire);
    }

    void put_i64_ordered(i32 offset, i64 value) noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i64>*>(data_ + offset);
        ptr->store(value, std::memory_order_release);
    }

    // --- CAS ---

    [[nodiscard]] bool compare_and_set_i32(i32 offset, i32 expected, i32 desired) noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i32>*>(data_ + offset);
        return ptr->compare_exchange_strong(expected, desired,
                                            std::memory_order_acq_rel);
    }

    [[nodiscard]] bool compare_and_set_i64(i32 offset, i64 expected, i64 desired) noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i64>*>(data_ + offset);
        return ptr->compare_exchange_strong(expected, desired,
                                            std::memory_order_acq_rel);
    }

    // --- Atomic add ---

    [[nodiscard]] i32 get_and_add_i32(i32 offset, i32 increment) noexcept
    {
        assert(offset >= 0 && offset + 4 <= capacity_);
        assert(is_aligned<i32>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i32>*>(data_ + offset);
        return ptr->fetch_add(increment, std::memory_order_acq_rel);
    }

    [[nodiscard]] i64 get_and_add_i64(i32 offset, i64 increment) noexcept
    {
        assert(offset >= 0 && offset + 8 <= capacity_);
        assert(is_aligned<i64>(offset));
        auto* ptr = reinterpret_cast<std::atomic<i64>*>(data_ + offset);
        return ptr->fetch_add(increment, std::memory_order_acq_rel);
    }

    // --- Copy ---

    void put_bytes(i32 offset, const void* src, i32 length) noexcept
    {
        assert(offset >= 0 && offset + length <= capacity_);
        std::memcpy(data_ + offset, src, static_cast<size_t>(length));
    }

    /// Copy bytes from another UnsafeBuffer into this buffer.
    void put_bytes(i32 dst_offset, const UnsafeBuffer& src, i32 src_offset, i32 length) noexcept
    {
        assert(dst_offset >= 0 && dst_offset + length <= capacity_);
        assert(src_offset >= 0 && src_offset + length <= src.capacity_);
        std::memcpy(data_ + dst_offset, src.data_ + src_offset, static_cast<size_t>(length));
    }

    void get_bytes(i32 offset, void* dst, i32 length) const noexcept
    {
        assert(offset >= 0 && offset + length <= capacity_);
        std::memcpy(dst, data_ + offset, static_cast<size_t>(length));
    }

    void set_memory(i32 offset, i32 length, u8 value) noexcept
    {
        assert(offset >= 0 && offset + length <= capacity_);
        std::memset(data_ + offset, value, static_cast<size_t>(length));
    }

private:
    template <typename T>
    [[nodiscard]] bool is_aligned(i32 offset) const noexcept
    {
        return (reinterpret_cast<uintptr_t>(data_ + offset) % alignof(T)) == 0;
    }

    std::byte* data_ = nullptr;
    i32 capacity_ = 0;
};

} // namespace caeron::concurrent
