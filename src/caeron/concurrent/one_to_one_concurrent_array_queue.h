#pragma once

#include "caeron/common/types.h"

#include <atomic>
#include <bit>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace caeron::concurrent {

/// Lock-free One-to-One (SPSC) concurrent array queue.
///
/// Uses a power-of-two sized circular buffer with separate head and tail
/// indices. Only one thread may call enqueue, and only one thread may call
/// dequeue.
///
/// Template parameter T must be default-constructible and movable.
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

        // Allocate with proper alignment for T.
        storage_ = static_cast<T*>(::operator new(sizeof(T) * static_cast<size_t>(capacity_),
                                                   std::align_val_t{alignof(T)}));

        // Default-construct each slot.
        for (i32 i = 0; i < capacity_; ++i)
            new (storage_ + i) T{};
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

        storage_[tail & mask_] = std::move(value);
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

        out = std::move(storage_[head & mask_]);
        // Re-construct a default T in the slot so it stays in a valid state.
        new (storage_ + (head & mask_)) T{};
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
    alignas(std::hardware_destructive_interference_size)
        std::atomic<i32> head_{0};
    alignas(std::hardware_destructive_interference_size)
        std::atomic<i32> tail_{0};
};

} // namespace caeron::concurrent
