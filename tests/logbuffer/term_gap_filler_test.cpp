#include <gtest/gtest.h>
#include "logbuffer/term_gap_filler.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/log_buffer_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"
#include "protocol/header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermGapFillerTest : public ::testing::Test
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

        // Write a default DATA header into the metadata at DEFAULT_FRAME_HEADER_OFFSET
        write_default_header(
            meta_buffer_,
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_OFFSET,
            0,  // frame_length (will be overwritten)
            1,  // session_id
            10, // stream_id
            100 // term_id
        );
        // Store the header length
        meta_buffer_.put_i32(
            LogBufferDescriptor::DEFAULT_FRAME_HEADER_LENGTH_OFFSET,
            DataHeaderFlyweight::HEADER_LENGTH);
    }
};

TEST_F(TermGapFillerTest, FillEmptyGap)
{
    // Gap from offset 0 to 64 (all zeros)
    bool filled = TermGapFiller::try_fill_gap(meta_buffer_, term_buffer_, 100, 0, 64);

    EXPECT_TRUE(filled);

    // Should be a padding frame
    EXPECT_EQ(FrameDescriptor::frame_type(term_buffer_, 0), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), 64);
    EXPECT_EQ(FrameDescriptor::frame_term_id(term_buffer_, 0), 100);
}

TEST_F(TermGapFillerTest, SkipIfDataArrived)
{
    // Write some data in the gap
    term_buffer_.put_i32(32, 999);

    bool filled = TermGapFiller::try_fill_gap(meta_buffer_, term_buffer_, 100, 0, 64);

    EXPECT_FALSE(filled);
    // Original data should be untouched
    EXPECT_EQ(term_buffer_.get_i32(32), 999);
}

TEST_F(TermGapFillerTest, FillGapAtNonZeroOffset)
{
    bool filled = TermGapFiller::try_fill_gap(meta_buffer_, term_buffer_, 100, 128, 64);

    EXPECT_TRUE(filled);
    EXPECT_EQ(FrameDescriptor::frame_type(term_buffer_, 128), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 128), 64);
}

TEST_F(TermGapFillerTest, FillLargerGap)
{
    // Gap of 128 bytes (4 frames worth)
    bool filled = TermGapFiller::try_fill_gap(meta_buffer_, term_buffer_, 100, 0, 128);

    EXPECT_TRUE(filled);
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), 128);
}
