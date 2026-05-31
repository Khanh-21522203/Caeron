#include <gtest/gtest.h>
#include "logbuffer/log_buffer_unblocker.h"
#include "logbuffer/log_buffer_descriptor.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"
#include "protocol/header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class LogBufferUnblockerTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    static constexpr i32 META_LENGTH = LogBufferDescriptor::LOG_META_DATA_LENGTH;

    std::byte term0_storage_[TERM_LENGTH]{};
    std::byte term1_storage_[TERM_LENGTH]{};
    std::byte term2_storage_[TERM_LENGTH]{};
    std::byte meta_storage_[META_LENGTH]{};

    UnsafeBuffer term_buffers_[3]{
        {term0_storage_, TERM_LENGTH},
        {term1_storage_, TERM_LENGTH},
        {term2_storage_, TERM_LENGTH}
    };
    UnsafeBuffer meta_buffer_{meta_storage_, META_LENGTH};

    void SetUp() override
    {
        std::memset(term0_storage_, 0, sizeof(term0_storage_));
        std::memset(term1_storage_, 0, sizeof(term1_storage_));
        std::memset(term2_storage_, 0, sizeof(term2_storage_));
        std::memset(meta_storage_, 0, sizeof(meta_storage_));

        // Write default header into metadata
        write_default_header(
            meta_buffer_,
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_OFFSET,
            0, 1, 10, 100);
        meta_buffer_.put_i32(
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_LENGTH_OFFSET,
            DataHeaderFlyweight::HEADER_LENGTH);

        // Initialize tail counters for all 3 partitions.
        // At active_term_count=0 with initial_term_id=100:
        //   Partition 0 is active with term_id=100
        //   Partition 1 is pre-initialized with term_id=98 (= 100 - PARTITION_COUNT)
        //     so rotate_log from term 0 → 1 finds the expected_term_id
        //   Partition 2 is pre-initialized with term_id=99 (= 100 - PARTITION_COUNT + 1)
        //     so rotate_log from term 1 → 2 finds the expected_term_id
        LogBufferDescriptor::initialise_tail_with_term_id(meta_buffer_, 0, 100);
        LogBufferDescriptor::initialise_tail_with_term_id(meta_buffer_, 1, 98);
        LogBufferDescriptor::initialise_tail_with_term_id(meta_buffer_, 2, 99);

        // Set active_term_count = 0 (partition 0 is active)
        meta_buffer_.put_i32(LogBufferDescriptor::ACTIVE_TERM_COUNT_OFFSET, 0);
    }
};

TEST_F(LogBufferUnblockerTest, UnblockStuckPublisher)
{
    // Simulate a stuck publisher at offset 0 of partition 0.
    // Write a negative frame_length (partially written).
    term0_storage_[0] = std::byte(0xC0); // -64 in little-endian i32
    term0_storage_[1] = std::byte(0xFF);
    term0_storage_[2] = std::byte(0xFF);
    term0_storage_[3] = std::byte(0xFF);

    // blocked_position = 0 (term_count=0, offset=0)
    bool unblocked = LogBufferUnblocker::unblock(
        term_buffers_, meta_buffer_, 0, TERM_LENGTH);

    EXPECT_TRUE(unblocked);
}

TEST_F(LogBufferUnblockerTest, NoActionOnCompleteFrame)
{
    // Write a complete frame at offset 0 of partition 0
    write_default_header(term_buffers_[0], 0, 64, 1, 10, 100);

    bool unblocked = LogBufferUnblocker::unblock(
        term_buffers_, meta_buffer_, 0, TERM_LENGTH);

    EXPECT_FALSE(unblocked);
}
