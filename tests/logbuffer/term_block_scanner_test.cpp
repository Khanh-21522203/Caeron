#include <gtest/gtest.h>
#include "logbuffer/term_block_scanner.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"
#include "protocol/header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermBlockScannerTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    std::byte term_storage_[TERM_LENGTH]{};
    UnsafeBuffer term_buffer_{term_storage_, TERM_LENGTH};

    void SetUp() override { std::memset(term_storage_, 0, sizeof(term_storage_)); }

    void write_frame(i32 offset, i32 length)
    {
        write_default_header(term_buffer_, offset, length, 1, 10, 100);
    }
};

TEST_F(TermBlockScannerTest, ScanSingleFrame)
{
    write_frame(0, 64);

    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    EXPECT_EQ(end, 64);
}

TEST_F(TermBlockScannerTest, ScanMultipleFrames)
{
    write_frame(0, 64);
    write_frame(64, 64);
    write_frame(128, 64);

    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    EXPECT_EQ(end, 192);
}

TEST_F(TermBlockScannerTest, StopAtUnpublished)
{
    write_frame(0, 64);
    // offset 64 is empty

    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    EXPECT_EQ(end, 64);
}

TEST_F(TermBlockScannerTest, StopAtPaddingMidBlock)
{
    write_frame(0, 64);
    // Padding at offset 64 (type = PAD, positive length)
    term_buffer_.put_i32(64, 32); // frame_length = 32
    term_buffer_.put_u16(64 + FrameDescriptor::TYPE_FIELD_OFFSET, protocol::HeaderFlyweight::HDR_TYPE_PAD);
    write_frame(128, 64);

    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    // Should stop before the padding
    EXPECT_EQ(end, 64);
}

TEST_F(TermBlockScannerTest, ConsumePaddingAtStart)
{
    // Padding at offset 0 (type = PAD, positive length)
    term_buffer_.put_i32(0, 32); // frame_length = 32
    term_buffer_.put_u16(FrameDescriptor::TYPE_FIELD_OFFSET, protocol::HeaderFlyweight::HDR_TYPE_PAD);
    write_frame(64, 64);

    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    // Should consume the padding as a single block
    EXPECT_EQ(end, 32);
}

TEST_F(TermBlockScannerTest, RespectLimitOffset)
{
    write_frame(0, 64);
    write_frame(64, 64);
    write_frame(128, 64);

    auto end = TermBlockScanner::scan(term_buffer_, 0, 100);

    // Can only fit one 64-byte frame before limit
    EXPECT_EQ(end, 64);
}

TEST_F(TermBlockScannerTest, ScanNothing)
{
    auto end = TermBlockScanner::scan(term_buffer_, 0, TERM_LENGTH);

    EXPECT_EQ(end, 0);
}
