#include <gtest/gtest.h>
#include "logbuffer/term_unblocker.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/log_buffer_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"
#include "protocol/header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermUnblockerTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    static constexpr i32 META_LENGTH = LogBufferDescriptor::LOG_META_DATA_LENGTH;

    std::byte term_storage_[TERM_LENGTH]{};
    std::byte meta_storage_[META_LENGTH]{};
    UnsafeBuffer term_buffer_{term_storage_, TERM_LENGTH};
    UnsafeBuffer meta_buffer_{meta_storage_, META_LENGTH};

    void SetUp() override
    {
        std::memset(term_storage_, 0, sizeof(term_storage_));
        std::memset(meta_storage_, 0, sizeof(meta_storage_));

        // Write default header into metadata
        write_default_header(
            meta_buffer_,
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_OFFSET,
            0, 1, 10, 100);
        meta_buffer_.put_i32(
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_LENGTH_OFFSET,
            DataHeaderFlyweight::HEADER_LENGTH);
    }
};

TEST_F(TermUnblockerTest, NoActionOnCompleteFrame)
{
    // Write a complete frame
    write_default_header(term_buffer_, 0, 64, 1, 10, 100);

    auto status = TermUnblocker::unblock(
        meta_buffer_, term_buffer_, 0, 64, 100);

    EXPECT_EQ(status, UnblockStatus::NO_ACTION);
}

TEST_F(TermUnblockerTest, UnblockNegativeLength)
{
    // Simulate a partially written frame (negative length)
    term_buffer_.put_i32(0, -64);

    auto status = TermUnblocker::unblock(
        meta_buffer_, term_buffer_, 0, 64, 100);

    EXPECT_EQ(status, UnblockStatus::UNBLOCKED);

    // Should now be a padding frame with positive length
    EXPECT_EQ(FrameDescriptor::frame_type(term_buffer_, 0), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), 64);
}

TEST_F(TermUnblockerTest, UnblockZeroLengthWithGap)
{
    // Empty slot at 0, data at 128, tail at 192 (past the data frame)
    write_default_header(term_buffer_, 128, 64, 1, 10, 100);

    auto status = TermUnblocker::unblock(
        meta_buffer_, term_buffer_, 0, 192, 100);

    EXPECT_EQ(status, UnblockStatus::UNBLOCKED);

    // Should have filled [0, 128) as padding
    EXPECT_EQ(FrameDescriptor::frame_type(term_buffer_, 0), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), 128);
}

TEST_F(TermUnblockerTest, UnblockToEndOfTerm)
{
    // All slots from 0 to tail are empty — tail is at TERM_LENGTH
    auto status = TermUnblocker::unblock(
        meta_buffer_, term_buffer_, 0, TERM_LENGTH, 100);

    EXPECT_EQ(status, UnblockStatus::UNBLOCKED_TO_END);

    // Should have filled from blocked_offset to tail as padding
    EXPECT_EQ(FrameDescriptor::frame_type(term_buffer_, 0), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), TERM_LENGTH);
}
