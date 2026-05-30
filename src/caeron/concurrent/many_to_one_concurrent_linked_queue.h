#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace caeron::concurrent {

/// Lock-free Many-to-One (MPSC) concurrent linked queue.
///
/// Uses a sentinel node so that enqueue always has a valid node to CAS against.
/// The single consumer thread reads from the head side.
///
/// Template parameter T must be default-constructible.
template <typename T>
class ManyToOneConcurrentLinkedQueue
{
    struct Node
    {
        T value{};
        std::atomic<Node*> next{nullptr};
    };

public:
    ManyToOneConcurrentLinkedQueue()
    {
        // Allocate a sentinel node. This node is never consumed.
        auto* sentinel = new Node;
        head_.store(sentinel, std::memory_order_relaxed);
        tail_.store(sentinel, std::memory_order_relaxed);
    }

    ~ManyToOneConcurrentLinkedQueue()
    {
        // Drain remaining nodes.
        Node* current = head_.load(std::memory_order_relaxed);
        while (current != nullptr)
        {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

    // Non-copyable, non-movable (shared state).
    ManyToOneConcurrentLinkedQueue(const ManyToOneConcurrentLinkedQueue&) = delete;
    ManyToOneConcurrentLinkedQueue& operator=(const ManyToOneConcurrentLinkedQueue&) = delete;
    ManyToOneConcurrentLinkedQueue(ManyToOneConcurrentLinkedQueue&&) = delete;
    ManyToOneConcurrentLinkedQueue& operator=(ManyToOneConcurrentLinkedQueue&&) = delete;

    /// Enqueue a value. Safe to call from multiple threads.
    void enqueue(T value)
    {
        auto* node = new Node{std::move(value), nullptr};

        Node* prev_tail = tail_.exchange(node, std::memory_order_acq_rel);
        // Link the previous tail's next to this new node, publishing it.
        prev_tail->next.store(node, std::memory_order_release);
    }

    /// Try to dequeue a value. Must be called from a single consumer thread.
    ///
    /// @return true if a value was dequeued into \p out, false if the queue is empty.
    bool try_dequeue(T& out)
    {
        Node* head = head_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);

        if (next == nullptr)
            return false;

        // Move the value out of the next node (the sentinel holds no useful value).
        out = std::move(next->value);

        // Advance head: the old head becomes the new sentinel.
        head_.store(next, std::memory_order_release);
        delete head;

        return true;
    }

    /// Check if the queue is empty (approximate, single consumer).
    [[nodiscard]] bool empty() const noexcept
    {
        Node* head = head_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);
        return next == nullptr;
    }

private:
    std::atomic<Node*> head_{nullptr};
    std::atomic<Node*> tail_{nullptr};
};

} // namespace caeron::concurrent
