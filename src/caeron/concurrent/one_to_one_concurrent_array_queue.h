#pragma once

#include "caeron/common/types.h"

#include <atomic>
#include <bit>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace caeron::concurrent {

/// Lock-free One-to-One (SPSC) concurrent array queue.
///
/// Uses a power-of-two sized circular buffer with separate head and tail
/// indices. Only one thread may call enqueue, and only one thread may call
/// dequeue.
///
/// Template parameter T must be movable. Slots are left uninitialized until enqueued.
template <typename T>
class OneToOneConcurrentArrayQueue
{
public:
    explicit OneToOneConcurrentArrayQueue(i32 capacity)
    {
        if (capacity <= 0)
            throw std::invalid_argument("capacity must be positive");

        capacity_ = static_cast<i32>(std::bit_ceil(static_cast<u32>(capacity)));
        mask_ = capacity_ - 1;

        // Allocate raw storage with proper alignment for T.
        // Slots are left uninitialized — enqueue placement-news, try_dequeue destroys.
        storage_ = static_cast<T*>(::operator new(sizeof(T) * static_cast<size_t>(capacity_),
                                                   std::align_val_t{alignof(T)}));
    }

    ~OneToOneConcurrentArrayQueue()
    {
        if (storage_ != nullptr)
        {
            const i32 head = head_.load(std::memory_order_relaxed);
            const i32 tail = tail_.load(std::memory_order_relaxed);
            for (i32 i = head; i < tail; ++i)
                storage_[i & mask_].~T();

            ::operator delete(storage_, std::align_val_t{alignof(T)});
        }
    }

    // Non-copyable, non-movable (shared state).
    OneToOneConcurrentArrayQueue(const OneToOneConcurrentArrayQueue&) = delete;
    OneToOneConcurrentArrayQueue& operator=(const OneToOneConcurrentArrayQueue&) = delete;
    OneToOneConcurrentArrayQueue(OneToOneConcurrentArrayQueue&&) = delete;
    OneToOneConcurrentArrayQueue& operator=(OneToOneConcurrentArrayQueue&&) = delete;

    /// Enqueue a value. Must be called from the single producer thread.
    ///
    /// @return true if the value was enqueued, false if the queue is full.
    bool enqueue(T value)
    {
        const i32 tail = tail_.load(std::memory_order_relaxed);
        const i32 head = head_.load(std::memory_order_acquire);

        if (tail - head >= capacity_)
            return false;

        T* slot = &storage_[tail & mask_];
        // Placement-new directly. Slots are raw uninitialized storage — no prior
        // object exists to destroy. After the first enqueue/dequeue cycle, slots
        // are either initialized (enqueued) or destroyed (dequeued).
        ::new (slot) T(std::move(value));
        tail_.store(tail + 1, std::memory_order_release);

        return true;
    }

    /// Try to dequeue a value. Must be called from the single consumer thread.
    ///
    /// @return true if a value was dequeued into \p out, false if the queue is empty.
    bool try_dequeue(T& out)
    {
        const i32 head = head_.load(std::memory_order_relaxed);
        const i32 tail = tail_.load(std::memory_order_acquire);

        if (head == tail)
            return false;

        T* slot = &storage_[head & mask_];
        out = std::move(*slot);
        if constexpr (!std::is_trivially_destructible_v<T>)
            std::destroy_at(slot);

        head_.store(head + 1, std::memory_order_release);

        return true;
    }

    /// Current number of elements in the queue (approximate).
    [[nodiscard]] i32 size() const noexcept
    {
        const i32 tail = tail_.load(std::memory_order_relaxed);
        const i32 head = head_.load(std::memory_order_relaxed);
        return tail - head;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] i32 capacity() const noexcept { return capacity_; }

private:
    T* storage_ = nullptr;
    i32 capacity_ = 0;
    i32 mask_ = 0;

    // Align head and tail to separate cache lines to avoid false sharing.
    // Using 64 bytes (typical cache line) instead of hardware_destructive_interference_size
    // to avoid compiler warnings about ABI instability.
    static constexpr std::size_t CACHE_LINE = 64;
    alignas(CACHE_LINE) std::atomic<i32> head_{0};
    alignas(CACHE_LINE) std::atomic<i32> tail_{0};
};

} // namespace caeron::concurrent
