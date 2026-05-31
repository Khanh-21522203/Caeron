#include <gtest/gtest.h>
#include "logbuffer/term_gap_scanner.h"
#include "logbuffer/frame_descriptor.h"
#include "logbuffer/header_writer.h"
#include "protocol/data_header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;

struct TestGapHandler : GapHandler
{
    struct Gap { i32 term_id; i32 offset; i32 length; };
    std::vector<Gap> gaps;

    void on_gap(i32 term_id, i32 offset, i32 length) override
    {
        gaps.push_back({term_id, offset, length});
    }
};

class TermGapScannerTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    std::byte term_storage_[TERM_LENGTH]{};
    UnsafeBuffer term_buffer_{term_storage_, TERM_LENGTH};
    TestGapHandler handler_;

    void SetUp() override
    {
        std::memset(term_storage_, 0, sizeof(term_storage_));
        handler_.gaps.clear();
    }

    void write_frame(i32 offset, i32 length, i32 term_id = 100)
    {
        write_default_header(term_buffer_, offset, length, 1, 10, term_id);
    }
};

TEST_F(TermGapScannerTest, NoGapWhenContiguous)
{
    write_frame(0, 64);
    write_frame(64, 64);
    write_frame(128, 64);

    (void)TermGapScanner::scan_for_gap(term_buffer_, 100, 0, 192, handler_);

    EXPECT_TRUE(handler_.gaps.empty());
}

TEST_F(TermGapScannerTest, DetectSimpleGap)
{
    write_frame(0, 64);
    // gap at offset 64 (empty)
    write_frame(128, 64);

    (void)TermGapScanner::scan_for_gap(term_buffer_, 100, 0, 192, handler_);

    ASSERT_EQ(handler_.gaps.size(), 1u);
    EXPECT_EQ(handler_.gaps[0].term_id, 100);
    EXPECT_EQ(handler_.gaps[0].offset, 64);
    EXPECT_EQ(handler_.gaps[0].length, 64);
}

TEST_F(TermGapScannerTest, DetectGapAtStart)
{
    // gap at offset 0 (empty)
    write_frame(64, 64);

    (void)TermGapScanner::scan_for_gap(term_buffer_, 100, 0, 128, handler_);

    ASSERT_EQ(handler_.gaps.size(), 1u);
    EXPECT_EQ(handler_.gaps[0].offset, 0);
}

TEST_F(TermGapScannerTest, NoGapWhenEmpty)
{
    // All zeros — no frames at all
    (void)TermGapScanner::scan_for_gap(term_buffer_, 100, 0, 64, handler_);

    // Gap from 0 to 64
    ASSERT_EQ(handler_.gaps.size(), 1u);
    EXPECT_EQ(handler_.gaps[0].offset, 0);
    EXPECT_EQ(handler_.gaps[0].length, 64);
}
