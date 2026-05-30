#include "caeron/concurrent/many_to_one_ring_buffer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::concurrent;

class RingBufferTest : public ::testing::Test
{
protected:
    static constexpr i32 DATA_REGION = 1024 * 1024;
    static constexpr i32 BUFFER_SIZE = ManyToOneRingBuffer::HEADER_LENGTH + DATA_REGION;

    void SetUp() override
    {
        storage_ = std::make_unique<std::byte[]>(BUFFER_SIZE);
        std::memset(storage_.get(), 0, BUFFER_SIZE);
        buffer_ = UnsafeBuffer{storage_.get(), BUFFER_SIZE};
    }

    std::unique_ptr<std::byte[]> storage_;
    UnsafeBuffer buffer_;
};

TEST_F(RingBufferTest, WriteAndReadSingleMessage)
{
    ManyToOneRingBuffer ring{buffer_};

    i32 msg = 42;
    EXPECT_TRUE(ring.write(1, &msg, sizeof(msg)));

    i32 received = 0;
    i32 count = ring.read([&](i32 type, const std::byte* data, i32 len) {
        EXPECT_EQ(type, 1);
        EXPECT_EQ(len, static_cast<i32>(sizeof(i32)));
        std::memcpy(&received, data, sizeof(i32));
    });

    EXPECT_EQ(count, 1);
    EXPECT_EQ(received, 42);
}

TEST_F(RingBufferTest, WriteAndReadMultipleMessages)
{
    ManyToOneRingBuffer ring{buffer_};

    for (i32 i = 0; i < 100; ++i)
        EXPECT_TRUE(ring.write(i % 10 + 1, &i, sizeof(i)));

    std::vector<i32> received;
    ring.read([&](i32 type, const std::byte* data, i32 len) {
        i32 val;
        std::memcpy(&val, data, sizeof(i32));
        received.push_back(val);
    });

    EXPECT_EQ(received.size(), 100u);
    for (i32 i = 0; i < 100; ++i)
        EXPECT_EQ(received[i], i);
}

TEST_F(RingBufferTest, ReadEmptyReturnsZero)
{
    ManyToOneRingBuffer ring{buffer_};

    i32 count = ring.read([](i32, const std::byte*, i32) {});
    EXPECT_EQ(count, 0);
}

TEST_F(RingBufferTest, WriteUntilFull)
{
    ManyToOneRingBuffer ring{buffer_};

    i32 msg = 1;
    i32 written = 0;
    while (ring.write(1, &msg, sizeof(msg)))
        ++written;

    EXPECT_GT(written, 0);
    EXPECT_FALSE(ring.write(1, &msg, sizeof(msg)));

    i32 read_count = 0;
    ring.read([&](i32, const std::byte*, i32) { ++read_count; });
    EXPECT_EQ(read_count, written);
}

TEST_F(RingBufferTest, WrapWithPadding)
{
    // Small buffer: 128 (header) + 256 (data) = 384 bytes.
    static constexpr i32 SMALL_DATA = 256;
    static constexpr i32 SMALL_SIZE = ManyToOneRingBuffer::HEADER_LENGTH + SMALL_DATA;
    auto small_storage = std::make_unique<std::byte[]>(SMALL_SIZE);
    std::memset(small_storage.get(), 0, SMALL_SIZE);
    UnsafeBuffer small_buf{small_storage.get(), SMALL_SIZE};
    ManyToOneRingBuffer ring{small_buf};

    i32 payload[3];
    i32 written = 0;
    payload[0] = payload[1] = payload[2] = written;
    while (ring.write(1, payload, sizeof(payload)))
    {
        ++written;
        payload[0] = payload[1] = payload[2] = written;
    }
    EXPECT_GT(written, 0);

    i32 read_count = 0;
    ring.read([&](i32, const std::byte*, i32) { ++read_count; });
    EXPECT_EQ(read_count, written);

    i32 written2 = 0;
    payload[0] = payload[1] = payload[2] = written;
    while (ring.write(1, payload, sizeof(payload)))
    {
        ++written2;
        payload[0] = payload[1] = payload[2] = written + written2;
    }
    EXPECT_GT(written2, 0);

    i32 read_count2 = 0;
    ring.read([&](i32, const std::byte*, i32) { ++read_count2; });
    EXPECT_EQ(read_count2, written2);
}

TEST_F(RingBufferTest, PaddingHeadProgress)
{
    // Directly construct the race: padding consumed, wrapped record unpublished.
    // Verifies that read() publishes head progress even when messages_read == 0.
    static constexpr i32 SMALL_DATA = 256;
    static constexpr i32 SMALL_SIZE = ManyToOneRingBuffer::HEADER_LENGTH + SMALL_DATA;
    auto storage = std::make_unique<std::byte[]>(SMALL_SIZE);
    std::memset(storage.get(), 0, SMALL_SIZE);
    UnsafeBuffer buf{storage.get(), SMALL_SIZE};

    ManyToOneRingBuffer ring{buf};

    // Write one message. record_length = 8 + 4 = 12, aligned to 16.
    i32 msg = 42;
    EXPECT_TRUE(ring.write(1, &msg, sizeof(msg)));

    // Read it to advance head to 16.
    i32 count = ring.read([](i32, const std::byte*, i32) {});
    EXPECT_EQ(count, 1);

    // Now simulate the intermediate state:
    //   - head = 16 (consumer read one message)
    //   - Producer CAS'd tail to 16 + 256 = 272 (full wrap)
    //   - Producer wrote padding at data offset + 16 = 144 (length = -(256-16) = -240)
    //   - Producer hasn't written the wrapped record at data offset + 0 = 128
    static constexpr i32 DATA_OFFSET = ManyToOneRingBuffer::HEADER_LENGTH;
    static constexpr i32 MSG_SIZE = 16; // align(8 + 4, 4)

    // Write padding at the current head position.
    buf.put_i32_ordered(DATA_OFFSET + MSG_SIZE, -(SMALL_DATA - MSG_SIZE));

    // Advance tail to simulate producer CAS.
    buf.put_i64_ordered(64, MSG_SIZE + SMALL_DATA);

    // Wrapped record at DATA_OFFSET is still 0 (unpublished).
    EXPECT_EQ(buf.get_i32_ordered(DATA_OFFSET), 0);

    // Call read(). Before the fix: sees padding, clears it, advances next_head to 256,
    // sees record_length == 0, breaks with messages_read == 0, head NOT published.
    // After the fix: head is published to 256 even though messages_read == 0.
    i32 read_count = ring.read([](i32, const std::byte*, i32) {});
    EXPECT_EQ(read_count, 0);

    // Verify head was advanced past the padding to SMALL_DATA (256).
    i64 head = buf.get_i64_ordered(0);
    EXPECT_EQ(head, SMALL_DATA);

    // Now simulate the producer writing the wrapped record at DATA_OFFSET.
    i32 wrapped_msg = 99;
    buf.put_bytes(DATA_OFFSET + ManyToOneRingBuffer::MSG_HEADER_LENGTH,
                  &wrapped_msg, sizeof(wrapped_msg));
    buf.put_i32(DATA_OFFSET + SIZE_OF_INT, 1);
    buf.put_i32_ordered(DATA_OFFSET,
                        ManyToOneRingBuffer::MSG_HEADER_LENGTH + static_cast<i32>(sizeof(i32)));

    // Advance tail past the wrapped record.
    buf.put_i64_ordered(64, SMALL_DATA + MSG_SIZE);

    // read() should now pick up the wrapped record.
    i32 wrapped_received = 0;
    i32 read_count2 = ring.read([&](i32 type, const std::byte* data, i32 len) {
        EXPECT_EQ(type, 1);
        EXPECT_EQ(len, static_cast<i32>(sizeof(i32)));
        std::memcpy(&wrapped_received, data, sizeof(i32));
    });
    EXPECT_EQ(read_count2, 1);
    EXPECT_EQ(wrapped_received, 99);
}

TEST_F(RingBufferTest, RejectPaddingMsgTypeId)
{
    ManyToOneRingBuffer ring{buffer_};
    i32 msg = 1;
    EXPECT_THROW(ring.write(-1, &msg, sizeof(msg)), std::invalid_argument);
}

TEST_F(RingBufferTest, RejectNegativeLength)
{
    ManyToOneRingBuffer ring{buffer_};
    EXPECT_THROW(ring.write(1, nullptr, -1), std::invalid_argument);
}

TEST_F(RingBufferTest, CorruptRecordThrows)
{
    ManyToOneRingBuffer ring{buffer_};

    i32 msg = 42;
    ring.write(1, &msg, sizeof(msg));

    buffer_.put_i32(ManyToOneRingBuffer::HEADER_LENGTH, 999999);

    EXPECT_THROW(
        ring.read([](i32, const std::byte*, i32) {}),
        std::runtime_error);
}

TEST_F(RingBufferTest, ProducerConsumerStress)
{
    ManyToOneRingBuffer ring{buffer_};

    constexpr int NUM_MESSAGES = 1000;
    std::atomic<int> total_read{0};
    std::atomic<bool> producer_done{false};

    {
        std::jthread producer([&]() {
            for (int i = 0; i < NUM_MESSAGES; ++i) {
                i32 msg = i;
                while (!ring.write(1, &msg, sizeof(msg)))
                    std::this_thread::yield();
            }
            producer_done.store(true, std::memory_order_release);
        });

        std::jthread consumer([&]() {
            while (!producer_done.load(std::memory_order_acquire) || total_read.load() < NUM_MESSAGES) {
                i32 n = ring.read([&](i32, const std::byte*, i32) {
                    total_read.fetch_add(1, std::memory_order_relaxed);
                });
                if (n == 0)
                    std::this_thread::yield();
            }
        });
    }

    EXPECT_EQ(total_read.load(), NUM_MESSAGES);
}
