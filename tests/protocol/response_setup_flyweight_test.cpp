#include "caeron/protocol/response_setup_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(ResponseSetupFlyweight, FieldAccessorsAtOffsetZero)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    ResponseSetupFlyweight rs{buf};

    rs.set_frame_length(20)
      .set_version(0)
      .set_flags(0)
      .set_type(HeaderFlyweight::HDR_TYPE_RSP_SETUP)
      .set_session_id(42)
      .set_stream_id(7)
      .set_response_session_id(100);

    EXPECT_EQ(rs.frame_length(), 20);
    EXPECT_EQ(rs.version(), 0);
    EXPECT_EQ(rs.flags(), 0);
    EXPECT_EQ(rs.type(), HeaderFlyweight::HDR_TYPE_RSP_SETUP);
    EXPECT_EQ(rs.session_id(), 42);
    EXPECT_EQ(rs.stream_id(), 7);
    EXPECT_EQ(rs.response_session_id(), 100);
}

TEST(ResponseSetupFlyweight, FieldAccessorsAtNonZeroOffset)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 64;
    ResponseSetupFlyweight rs{buf, offset};

    rs.set_frame_length(20)
      .set_version(1)
      .set_flags(0x04)
      .set_type(HeaderFlyweight::HDR_TYPE_RSP_SETUP)
      .set_session_id(99)
      .set_stream_id(3)
      .set_response_session_id(200);

    EXPECT_EQ(rs.frame_length(), 20);
    EXPECT_EQ(rs.version(), 1);
    EXPECT_EQ(rs.flags(), 0x04);
    EXPECT_EQ(rs.type(), HeaderFlyweight::HDR_TYPE_RSP_SETUP);
    EXPECT_EQ(rs.session_id(), 99);
    EXPECT_EQ(rs.stream_id(), 3);
    EXPECT_EQ(rs.response_session_id(), 200);

    // Verify data is at correct offset, not at offset 0
    EXPECT_EQ(buf.get_i32(offset + 8), 99);
    EXPECT_EQ(buf.get_i32(offset + 16), 200);
}

TEST(ResponseSetupFlyweight, HeaderLengthConstant)
{
    EXPECT_EQ(ResponseSetupFlyweight::HEADER_LENGTH, 20);
}
