#include <gtest/gtest.h>
#include "logbuffer/term_reader.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermReaderTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    std::byte term_storage_[TERM_LENGTH]{};
    UnsafeBuffer term_buffer_{term_storage_, TERM_LENGTH};
    Header header_{term_buffer_, 0, 0, 0, 0, 0};

    void SetUp() override { std::memset(term_storage_, 0, sizeof(term_storage_)); }

    void write_frame(i32 offset, i32 frame_length, i32 session_id, i32 stream_id, i32 term_id)
    {
        write_default_header(term_buffer_, offset, frame_length, session_id, stream_id, term_id);
    }
};

TEST_F(TermReaderTest, ReadSingleFrame)
{
    write_frame(0, 64, 1, 10, 100);

    i32 count = 0;
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32 offset, Header& hdr) {
            ++count;
            EXPECT_EQ(hdr.term_id, 100);
            EXPECT_EQ(hdr.session_id, 1);
        },
        10, header_);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(TermReader::fragments_read(result), 1);
    EXPECT_EQ(TermReader::offset(result), 64);
}

TEST_F(TermReaderTest, ReadMultipleFrames)
{
    write_frame(0, 64, 1, 10, 100);
    write_frame(64, 64, 1, 10, 100);
    write_frame(128, 64, 1, 10, 100);

    i32 count = 0;
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32, Header&) { ++count; },
        10, header_);

    EXPECT_EQ(count, 3);
    EXPECT_EQ(TermReader::offset(result), 192);
}

TEST_F(TermReaderTest, StopAtUnpublished)
{
    write_frame(0, 64, 1, 10, 100);
    // offset 64 is empty (zero) — should stop

    i32 count = 0;
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32, Header&) { ++count; },
        10, header_);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(TermReader::fragments_read(result), 1);
}

TEST_F(TermReaderTest, SkipPaddingFrames)
{
    write_frame(0, 64, 1, 10, 100);
    // Write a padding frame at offset 64 (type = PAD, positive length)
    term_buffer_.put_i32(64, 32); // frame_length = 32
    term_buffer_.put_u16(64 + FrameDescriptor::TYPE_FIELD_OFFSET, HeaderFlyweight::HDR_TYPE_PAD);
    write_frame(96, 64, 1, 10, 100);

    i32 count = 0;
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32, Header&) { ++count; },
        10, header_);

    // Should read 2 data frames, skipping the padding
    EXPECT_EQ(count, 2);
    EXPECT_EQ(TermReader::fragments_read(result), 2);
}

TEST_F(TermReaderTest, RespectsFragmentsLimit)
{
    write_frame(0, 64, 1, 10, 100);
    write_frame(64, 64, 1, 10, 100);
    write_frame(128, 64, 1, 10, 100);

    i32 count = 0;
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32, Header&) { ++count; },
        2, header_); // limit to 2

    EXPECT_EQ(count, 2);
    EXPECT_EQ(TermReader::offset(result), 128);
}

TEST_F(TermReaderTest, ReadNothingReturnsZero)
{
    auto result = TermReader::read(term_buffer_, 0,
        [&](i32, Header&) {},
        10, header_);

    EXPECT_EQ(TermReader::fragments_read(result), 0);
}
