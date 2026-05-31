#include <gtest/gtest.h>
#include "logbuffer/buffer_claim.h"
#include "logbuffer/frame_descriptor.h"
#include "protocol/data_header_flyweight.h"
#include "protocol/header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class BufferClaimTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    std::byte storage_[TERM_LENGTH]{};
    UnsafeBuffer buffer_{storage_, TERM_LENGTH};

    void SetUp() override { std::memset(storage_, 0, sizeof(storage_)); }
};

TEST_F(BufferClaimTest, WrapAndAccessors)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 128);

    EXPECT_EQ(claim.capacity(), 128);
    EXPECT_EQ(claim.offset(), DataHeaderFlyweight::HEADER_LENGTH);
    EXPECT_EQ(claim.length(), 128 - DataHeaderFlyweight::HEADER_LENGTH);
}

TEST_F(BufferClaimTest, CommitWritesFrameLength)
{
    BufferClaim claim;
    claim.wrap(buffer_, 64, 128);

    // Before commit: frame_length should be 0
    EXPECT_EQ(FrameDescriptor::frame_length(buffer_, 64), 0);

    claim.commit();

    // After commit: frame_length should be 128 (the capacity)
    EXPECT_EQ(FrameDescriptor::frame_length(buffer_, 64), 128);
}

TEST_F(BufferClaimTest, AbortWritesPaddingType)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 64);

    claim.abort();

    // After abort: type should be PAD and frame_length should be set
    EXPECT_EQ(FrameDescriptor::frame_type(buffer_, 0), protocol::HeaderFlyweight::HDR_TYPE_PAD);
    EXPECT_EQ(FrameDescriptor::frame_length(buffer_, 0), 64);
}

TEST_F(BufferClaimTest, PutBytesCopiesPayload)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 64);

    // Write some data
    const char* msg = "hello";
    UnsafeBuffer src{const_cast<char*>(msg), 5};
    claim.put_bytes(src, 0, 5);

    // Verify data is at offset + HEADER_LENGTH
    EXPECT_EQ(std::memcmp(storage_ + DataHeaderFlyweight::HEADER_LENGTH, "hello", 5), 0);
}

TEST_F(BufferClaimTest, HeaderTypeReadWrite)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 64);

    claim.header_type(protocol::HeaderFlyweight::HDR_TYPE_DATA);
    EXPECT_EQ(claim.header_type(), protocol::HeaderFlyweight::HDR_TYPE_DATA);
}

TEST_F(BufferClaimTest, FlagsReadWrite)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 64);

    claim.flags(DataHeaderFlyweight::UNFRAGMENTED);
    EXPECT_EQ(claim.flags(), DataHeaderFlyweight::UNFRAGMENTED);
}

TEST_F(BufferClaimTest, ReservedValueReadWrite)
{
    BufferClaim claim;
    claim.wrap(buffer_, 0, 64);

    claim.reserved_value(0x1234567890ABCDEF);
    EXPECT_EQ(claim.reserved_value(), 0x1234567890ABCDEF);
}
