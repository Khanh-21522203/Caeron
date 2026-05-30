#include "caeron/concurrent/many_to_one_concurrent_linked_queue.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::concurrent;

TEST(MpscQueueTest, EnqueueAndDequeue)
{
    ManyToOneConcurrentLinkedQueue<int> q;

    q.enqueue(1);
    q.enqueue(2);
    q.enqueue(3);

    int val = 0;
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(q.try_dequeue(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(q.try_dequeue(val));
}

TEST(MpscQueueTest, EmptyReturnsTrue)
{
    ManyToOneConcurrentLinkedQueue<int> q;
    EXPECT_TRUE(q.empty());
}

TEST(MpscQueueTest, NotEmptyAfterEnqueue)
{
    ManyToOneConcurrentLinkedQueue<int> q;
    q.enqueue(42);
    EXPECT_FALSE(q.empty());
}

TEST(MpscQueueTest, MultiProducerSingleConsumer)
{
    ManyToOneConcurrentLinkedQueue<int> q;

    constexpr int NUM_PRODUCERS = 4;
    constexpr int MSGS_PER_PRODUCER = 10000;
    const int TOTAL = NUM_PRODUCERS * MSGS_PER_PRODUCER;
    std::atomic<bool> producers_done{false};
    std::vector<int> received;
    std::mutex mtx;

    // Each producer encodes: producer_id * MSGS_PER_PRODUCER + sequence
    auto producer = [&](int id) {
        for (int i = 0; i < MSGS_PER_PRODUCER; ++i)
            q.enqueue(id * MSGS_PER_PRODUCER + i);
    };

    auto consumer = [&]() {
        while (!producers_done.load(std::memory_order_acquire) ||
               static_cast<int>(received.size()) < TOTAL) {
            int val;
            if (q.try_dequeue(val)) {
                std::lock_guard lock{mtx};
                received.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    };

    {
        std::vector<std::jthread> threads;
        for (int i = 0; i < NUM_PRODUCERS; ++i)
            threads.emplace_back(producer, i);
        threads.emplace_back(consumer);

        for (int i = 0; i < NUM_PRODUCERS; ++i)
            threads[i].join();
        producers_done.store(true, std::memory_order_release);
        threads.back().join();
    }

    ASSERT_EQ(static_cast<int>(received.size()), TOTAL);

    // Verify uniqueness: all messages must be distinct.
    std::vector<int> sorted = received;
    std::sort(sorted.begin(), sorted.end());
    auto last = std::unique(sorted.begin(), sorted.end());
    EXPECT_EQ(last - sorted.begin(), TOTAL) << "duplicate messages detected";

    // Verify per-producer FIFO order: for each producer, messages must be
    // in ascending sequence order.
    std::vector<std::vector<int>> per_producer(NUM_PRODUCERS);
    for (int val : received) {
        int id = val / MSGS_PER_PRODUCER;
        int seq = val % MSGS_PER_PRODUCER;
        ASSERT_GE(id, 0);
        ASSERT_LT(id, NUM_PRODUCERS);
        per_producer[id].push_back(seq);
    }
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        ASSERT_EQ(static_cast<int>(per_producer[p].size()), MSGS_PER_PRODUCER);
        for (int i = 1; i < MSGS_PER_PRODUCER; ++i) {
            // Within a single producer's messages, later messages must have
            // larger sequence numbers (FIFO order).
            EXPECT_GT(per_producer[p][i], per_producer[p][i - 1])
                << "producer " << p << " lost FIFO order at index " << i;
        }
    }
}
