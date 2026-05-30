#include "caeron/concurrent/one_to_one_concurrent_array_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

using namespace caeron;
using namespace caeron::concurrent;

TEST(SpscQueueTest, EnqueueAndDequeue)
{
    OneToOneConcurrentArrayQueue<int> q(16);

    EXPECT_TRUE(q.enqueue(1));
    EXPECT_TRUE(q.enqueue(2));
    EXPECT_TRUE(q.enqueue(3));

    int val = 0;
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(q.try_dequeue(val));
}

TEST(SpscQueueTest, EmptyAndSize)
{
    OneToOneConcurrentArrayQueue<int> q(16);

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);

    q.enqueue(42);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1);

    int val;
    q.try_dequeue(val);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, EnqueueUntilFull)
{
    OneToOneConcurrentArrayQueue<int> q(4);

    EXPECT_TRUE(q.enqueue(1));
    EXPECT_TRUE(q.enqueue(2));
    EXPECT_TRUE(q.enqueue(3));
    EXPECT_TRUE(q.enqueue(4));
    EXPECT_FALSE(q.enqueue(5)); // full

    int val;
    q.try_dequeue(val);
    EXPECT_TRUE(q.enqueue(5)); // now has space
}

TEST(SpscQueueTest, CapacityRoundsToPowerOfTwo)
{
    OneToOneConcurrentArrayQueue<int> q(5);
    EXPECT_EQ(q.capacity(), 8); // next power of two
}

TEST(SpscQueueTest, ProducerConsumerStress)
{
    OneToOneConcurrentArrayQueue<int> q(1024);

    constexpr int NUM_ITEMS = 100000;
    std::atomic<bool> producer_done{false};

    auto producer = [&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!q.enqueue(i))
                std::this_thread::yield();
        }
        producer_done.store(true, std::memory_order_release);
    };

    auto consumer = [&]() {
        int received = 0;
        int expected = 0;
        while (received < NUM_ITEMS) {
            int val;
            if (q.try_dequeue(val)) {
                EXPECT_EQ(val, expected);
                ++expected;
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
        return received;
    };

    std::jthread producer_thread{producer};
    std::jthread consumer_thread{consumer};

    producer_thread.join();
    consumer_thread.join();
}

TEST(SpscQueueTest, ThrowsOnZeroCapacity)
{
    EXPECT_THROW(OneToOneConcurrentArrayQueue<int> q(0), std::invalid_argument);
}

namespace {

struct NonTrivial
{
    static inline std::atomic<int> alive{0};

    int value = 0;

    NonTrivial() { alive.fetch_add(1, std::memory_order_relaxed); }
    explicit NonTrivial(int v) : value{v} { alive.fetch_add(1, std::memory_order_relaxed); }
    ~NonTrivial() { alive.fetch_sub(1, std::memory_order_relaxed); }

    NonTrivial(const NonTrivial& o) : value{o.value} { alive.fetch_add(1, std::memory_order_relaxed); }
    NonTrivial(NonTrivial&& o) noexcept : value{o.value} { o.value = 0; alive.fetch_add(1, std::memory_order_relaxed); }
    NonTrivial& operator=(const NonTrivial& o) { value = o.value; return *this; }
    NonTrivial& operator=(NonTrivial&& o) noexcept { value = o.value; o.value = 0; return *this; }
};

} // anonymous namespace

TEST(SpscQueueTest, NonTrivialTypeLifetime)
{
    NonTrivial::alive.store(0, std::memory_order_relaxed);

    {
        OneToOneConcurrentArrayQueue<NonTrivial> q(16);

        // Enqueue some elements
        for (int i = 0; i < 10; ++i)
            EXPECT_TRUE(q.enqueue(NonTrivial{i}));

        // Dequeue them
        for (int i = 0; i < 10; ++i) {
            NonTrivial val;
            EXPECT_TRUE(q.try_dequeue(val));
            EXPECT_EQ(val.value, i);
        }

        // Enqueue again to test reuse of destroyed slots
        for (int i = 10; i < 20; ++i)
            EXPECT_TRUE(q.enqueue(NonTrivial{i}));

        for (int i = 10; i < 20; ++i) {
            NonTrivial val;
            EXPECT_TRUE(q.try_dequeue(val));
            EXPECT_EQ(val.value, i);
        }
    } // q destroyed here

    // All NonTrivial objects should have been destroyed.
    EXPECT_EQ(NonTrivial::alive.load(std::memory_order_relaxed), 0);
}
