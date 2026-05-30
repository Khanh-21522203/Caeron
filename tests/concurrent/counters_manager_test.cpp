#include "caeron/concurrent/counters_manager.h"
#include "caeron/concurrent/atomic_counter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::concurrent;

class CountersManagerTest : public ::testing::Test
{
protected:
    static constexpr i32 NUM_COUNTERS = 1024;
    static constexpr i32 SLOT_SIZE = 64;
    static constexpr i32 METADATA_SIZE = NUM_COUNTERS * SLOT_SIZE;
    static constexpr i32 VALUES_SIZE = NUM_COUNTERS * CountersManager::COUNTER_LENGTH;

    void SetUp() override
    {
        metadata_ = std::make_unique<std::byte[]>(METADATA_SIZE);
        values_ = std::make_unique<std::byte[]>(VALUES_SIZE);
        std::memset(metadata_.get(), 0, METADATA_SIZE);
        std::memset(values_.get(), 0, VALUES_SIZE);
        metadata_buf_ = UnsafeBuffer{metadata_.get(), METADATA_SIZE};
        values_buf_ = UnsafeBuffer{values_.get(), VALUES_SIZE};
    }

    std::unique_ptr<std::byte[]> metadata_;
    std::unique_ptr<std::byte[]> values_;
    UnsafeBuffer metadata_buf_;
    UnsafeBuffer values_buf_;
};

TEST_F(CountersManagerTest, AllocateAndFree)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    i32 id = mgr.allocate(1, nullptr, 0, "test", 4);
    EXPECT_GE(id, 0);
    EXPECT_EQ(mgr.get_type_id(id), 1);

    mgr.free(id);
    EXPECT_EQ(mgr.get_type_id(id), 0); // FREE
}

TEST_F(CountersManagerTest, AllocateMultiple)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    std::vector<i32> ids;
    for (int i = 0; i < 10; ++i)
        ids.push_back(mgr.allocate(i + 1, nullptr, 0, "test", 4));

    // All IDs should be unique
    std::sort(ids.begin(), ids.end());
    auto last = std::unique(ids.begin(), ids.end());
    EXPECT_EQ(last - ids.begin(), 10);

    for (auto id : ids)
        mgr.free(id);
}

TEST_F(CountersManagerTest, CounterValue)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    i32 id = mgr.allocate(1, nullptr, 0, "test", 4);
    mgr.set_counter_value(id, 42);
    EXPECT_EQ(mgr.get_counter_value(id), 42);

    AtomicCounter counter{id, values_buf_};
    EXPECT_EQ(counter.get(), 42);
    counter.set(100);
    EXPECT_EQ(counter.get(), 100);

    mgr.free(id);
}

TEST_F(CountersManagerTest, GetKeyAndLabel)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    u8 key[] = {0x01, 0x02, 0x03};
    i32 id = mgr.allocate(1, key, 3, "hello", 5);

    u8 key_out[16]{};
    i32 key_len = mgr.get_key(id, key_out, sizeof(key_out));
    EXPECT_EQ(key_len, 3);
    EXPECT_EQ(key_out[0], 0x01);
    EXPECT_EQ(key_out[1], 0x02);
    EXPECT_EQ(key_out[2], 0x03);

    char label_out[32]{};
    i32 label_len = mgr.get_label(id, label_out, sizeof(label_out));
    EXPECT_EQ(label_len, 5);
    EXPECT_STREQ(label_out, "hello");

    mgr.free(id);
}

TEST_F(CountersManagerTest, ForEach)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    mgr.allocate(1, nullptr, 0, "a", 1);
    mgr.allocate(2, nullptr, 0, "b", 1);
    mgr.allocate(3, nullptr, 0, "c", 1);

    int count = 0;
    mgr.forEach([&](i32 id, i32 type_id) {
        ++count;
        EXPECT_GE(type_id, 1);
        EXPECT_LE(type_id, 3);
    });
    EXPECT_EQ(count, 3);
}

TEST_F(CountersManagerTest, ThrowsWhenFull)
{
    // Only 4 slots
    constexpr i32 SMALL_COUNT = 4;
    constexpr i32 META_SIZE = SMALL_COUNT * SLOT_SIZE;
    constexpr i32 VAL_SIZE = SMALL_COUNT * CountersManager::COUNTER_LENGTH;

    auto meta = std::make_unique<std::byte[]>(META_SIZE);
    auto val = std::make_unique<std::byte[]>(VAL_SIZE);
    std::memset(meta.get(), 0, META_SIZE);
    std::memset(val.get(), 0, VAL_SIZE);

    CountersManager mgr{UnsafeBuffer{meta.get(), META_SIZE},
                        UnsafeBuffer{val.get(), VAL_SIZE}, SLOT_SIZE};

    for (int i = 0; i < SMALL_COUNT; ++i)
        mgr.allocate(i + 1, nullptr, 0, "test", 4);

    EXPECT_THROW(mgr.allocate(99, nullptr, 0, "overflow", 8), std::runtime_error);
}

TEST_F(CountersManagerTest, ConcurrentAllocate)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};

    constexpr int NUM_THREADS = 4;
    constexpr int ALLOCS_PER_THREAD = 100;
    std::atomic<int> total_allocated{0};
    std::vector<i32> all_ids;
    std::mutex mtx;

    auto worker = [&]() {
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            i32 id = mgr.allocate(i + 1, nullptr, 0, "t", 1);
            total_allocated.fetch_add(1);
            {
                std::lock_guard lock{mtx};
                all_ids.push_back(id);
            }
        }
    };

    std::vector<std::jthread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(total_allocated.load(), NUM_THREADS * ALLOCS_PER_THREAD);

    // All IDs should be unique
    std::sort(all_ids.begin(), all_ids.end());
    auto last = std::unique(all_ids.begin(), all_ids.end());
    EXPECT_EQ(static_cast<int>(last - all_ids.begin()), NUM_THREADS * ALLOCS_PER_THREAD);
}

TEST_F(CountersManagerTest, CounterIdOutOfRangeThrows)
{
    CountersManager mgr{metadata_buf_, values_buf_, SLOT_SIZE};
    EXPECT_THROW((void)mgr.get_type_id(-1), std::out_of_range);
    EXPECT_THROW((void)mgr.get_type_id(NUM_COUNTERS), std::out_of_range);
}
