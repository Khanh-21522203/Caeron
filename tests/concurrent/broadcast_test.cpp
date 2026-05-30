#include "caeron/concurrent/broadcast_transmitter.h"
#include "caeron/concurrent/broadcast_receiver.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::concurrent;

class BroadcastTest : public ::testing::Test
{
protected:
    static constexpr i32 DATA_REGION = 1024 * 1024;
    static constexpr i32 BUFFER_SIZE = BroadcastTransmitter::HEADER_LENGTH + DATA_REGION;

    void SetUp() override
    {
        storage_ = std::make_unique<std::byte[]>(BUFFER_SIZE);
        std::memset(storage_.get(), 0, BUFFER_SIZE);
        buffer_ = UnsafeBuffer{storage_.get(), BUFFER_SIZE};
    }

    std::unique_ptr<std::byte[]> storage_;
    UnsafeBuffer buffer_;
};

TEST_F(BroadcastTest, TransmitAndReceiveSingle)
{
    BroadcastTransmitter tx{buffer_};
    BroadcastReceiver rx{buffer_};

    i32 msg = 42;
    EXPECT_TRUE(tx.transmit(1, &msg, sizeof(msg)));

    i32 received = 0;
    i32 count = rx.receive([&](i32 type, const std::byte* data, i32 len) {
        EXPECT_EQ(type, 1);
        EXPECT_EQ(len, static_cast<i32>(sizeof(i32)));
        std::memcpy(&received, data, sizeof(i32));
    });

    EXPECT_EQ(count, 1);
    EXPECT_EQ(received, 42);
}

TEST_F(BroadcastTest, TransmitAndReceiveMultiple)
{
    BroadcastTransmitter tx{buffer_};
    BroadcastReceiver rx{buffer_};

    for (i32 i = 0; i < 100; ++i)
        EXPECT_TRUE(tx.transmit(i % 10 + 1, &i, sizeof(i)));

    std::vector<i32> received;
    rx.receive([&](i32 type, const std::byte* data, i32 len) {
        i32 val;
        std::memcpy(&val, data, sizeof(i32));
        received.push_back(val);
    });

    EXPECT_EQ(received.size(), 100u);
    for (i32 i = 0; i < 100; ++i)
        EXPECT_EQ(received[i], i);
}

TEST_F(BroadcastTest, ReceiveEmptyReturnsZero)
{
    BroadcastReceiver rx{buffer_};
    i32 count = rx.receive([](i32, const std::byte*, i32) {});
    EXPECT_EQ(count, 0);
}

TEST_F(BroadcastTest, ReceiverPublishesHead)
{
    BroadcastTransmitter tx{buffer_};
    BroadcastReceiver rx{buffer_};

    i32 msg = 1;
    for (int i = 0; i < 10; ++i)
        tx.transmit(1, &msg, sizeof(msg));

    rx.receive([](i32, const std::byte*, i32) {});

    // Head should have been written — transmitter should be able to write more
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(tx.transmit(1, &msg, sizeof(msg)));
}

TEST_F(BroadcastTest, TransmitUntilFull)
{
    BroadcastTransmitter tx{buffer_};

    i32 msg = 1;
    i32 count = 0;
    while (tx.transmit(1, &msg, sizeof(msg)))
        ++count;

    EXPECT_GT(count, 0);
    EXPECT_FALSE(tx.transmit(1, &msg, sizeof(msg)));
}

TEST_F(BroadcastTest, RejectPaddingMsgTypeId)
{
    BroadcastTransmitter tx{buffer_};
    i32 msg = 1;
    EXPECT_THROW(tx.transmit(-1, &msg, sizeof(msg)), std::invalid_argument);
}

TEST_F(BroadcastTest, CorruptRecordThrows)
{
    BroadcastTransmitter tx{buffer_};
    BroadcastReceiver rx{buffer_};

    i32 msg = 42;
    tx.transmit(1, &msg, sizeof(msg));

    // Corrupt the record length with a value larger than the data region capacity.
    buffer_.put_i32(BroadcastTransmitter::HEADER_LENGTH, DATA_REGION + 1);

    EXPECT_THROW(
        rx.receive([](i32, const std::byte*, i32) {}),
        std::runtime_error);
}

TEST_F(BroadcastTest, MultiReceiverStress)
{
    BroadcastTransmitter tx{buffer_};

    constexpr int NUM_RECEIVERS = 4;
    constexpr int NUM_MESSAGES = 10000;
    std::atomic<int> per_receiver_count{0};

    for (int i = 0; i < NUM_MESSAGES; ++i) {
        i32 msg = i;
        while (!tx.transmit(1, &msg, sizeof(msg)))
            std::this_thread::yield();
    }

    std::vector<std::jthread> threads;
    for (int i = 0; i < NUM_RECEIVERS; ++i) {
        threads.emplace_back([&]() {
            BroadcastReceiver rx{buffer_};
            rx.receive([&](i32, const std::byte*, i32) {
                per_receiver_count.fetch_add(1, std::memory_order_relaxed);
            });
        });
    }

    for (auto& t : threads)
        t.join();

    EXPECT_EQ(per_receiver_count.load(), NUM_RECEIVERS * NUM_MESSAGES);
}
