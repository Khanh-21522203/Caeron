#include <gtest/gtest.h>
#include "logbuffer/term_scanner.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermScannerTest : public ::testing::Test
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

TEST_F(TermScannerTest, ScanSingleFrame)
{
    write_frame(0, 64);

    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 4096);

    EXPECT_EQ(TermScanner::available(result), 64);
    EXPECT_EQ(TermScanner::padding(result), 0);
}

TEST_F(TermScannerTest, ScanMultipleContiguousFrames)
{
    write_frame(0, 64);
    write_frame(64, 64);
    write_frame(128, 64);

    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 4096);

    EXPECT_EQ(TermScanner::available(result), 192);
}

TEST_F(TermScannerTest, StopAtUnpublished)
{
    write_frame(0, 64);
    // offset 64 is empty

    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 4096);

    EXPECT_EQ(TermScanner::available(result), 64);
}

TEST_F(TermScannerTest, ScanNothingReturnsZero)
{
    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 4096);

    EXPECT_EQ(TermScanner::available(result), 0);
}

TEST_F(TermScannerTest, NegativeAvailableWhenExceedsMax)
{
    write_frame(0, 128);

    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 64);

    // First frame exceeds max_length — should return negative
    EXPECT_LT(TermScanner::available(result), 0);
}

TEST_F(TermScannerTest, RespectMaxLength)
{
    write_frame(0, 64);
    write_frame(64, 64);
    write_frame(128, 64);

    // max_length = 100 — can only fit one 64-byte frame
    auto result = TermScanner::scan_for_availability(term_buffer_, 0, 100);

    EXPECT_EQ(TermScanner::available(result), 64);
}
