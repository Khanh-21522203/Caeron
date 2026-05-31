#include "caeron/protocol/error_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(ErrorFlyweight, HeaderLengthConstant)
{
    EXPECT_EQ(ErrorFlyweight::HEADER_LENGTH, 40);
}

TEST(ErrorFlyweight, FieldOffsets)
{
    EXPECT_EQ(ErrorFlyweight::SESSION_ID_FIELD_OFFSET, 8);
    EXPECT_EQ(ErrorFlyweight::STREAM_ID_FIELD_OFFSET, 12);
    EXPECT_EQ(ErrorFlyweight::RECEIVER_ID_FIELD_OFFSET, 16);
    EXPECT_EQ(ErrorFlyweight::GROUP_TAG_FIELD_OFFSET, 24);
    EXPECT_EQ(ErrorFlyweight::ERROR_CODE_FIELD_OFFSET, 32);
    EXPECT_EQ(ErrorFlyweight::ERROR_STRING_FIELD_OFFSET, 36);
}

TEST(ErrorFlyweight, FieldAccessorsAtOffsetZero)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ErrorFlyweight err{buf};

    err.set_frame_length(64)
       .set_version(0)
       .set_flags(0)
       .set_type(HeaderFlyweight::HDR_TYPE_ERR)
       .set_session_id(42)
       .set_stream_id(7)
       .set_receiver_id(0x1234567890ABCDEFLL)
       .set_error_code(1001);

    EXPECT_EQ(err.frame_length(), 64);
    EXPECT_EQ(err.version(), 0);
    EXPECT_EQ(err.flags(), 0);
    EXPECT_EQ(err.type(), HeaderFlyweight::HDR_TYPE_ERR);
    EXPECT_EQ(err.session_id(), 42);
    EXPECT_EQ(err.stream_id(), 7);
    EXPECT_EQ(err.receiver_id(), 0x1234567890ABCDEFLL);
    EXPECT_EQ(err.error_code(), 1001);
}

TEST(ErrorFlyweight, FieldAccessorsAtNonZeroOffset)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 128;
    ErrorFlyweight err{buf, offset};

    err.set_frame_length(64)
       .set_version(2)
       .set_flags(0x08)
       .set_type(HeaderFlyweight::HDR_TYPE_ERR)
       .set_session_id(99)
       .set_stream_id(3)
       .set_receiver_id(0xDEADBEEF)
       .set_error_code(2002);

    EXPECT_EQ(err.frame_length(), 64);
    EXPECT_EQ(err.version(), 2);
    EXPECT_EQ(err.flags(), 0x08);
    EXPECT_EQ(err.type(), HeaderFlyweight::HDR_TYPE_ERR);
    EXPECT_EQ(err.session_id(), 99);
    EXPECT_EQ(err.stream_id(), 3);
    EXPECT_EQ(err.receiver_id(), 0xDEADBEEF);
    EXPECT_EQ(err.error_code(), 2002);

    // Verify data is at correct offset, not at offset 0
    EXPECT_EQ(buf.get_i32(offset + ErrorFlyweight::SESSION_ID_FIELD_OFFSET), 99);
    EXPECT_EQ(buf.get_i32(offset + ErrorFlyweight::ERROR_CODE_FIELD_OFFSET), 2002);
}

TEST(ErrorFlyweight, GroupTag)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ErrorFlyweight err{buf};

    err.set_group_tag(0x1234567890ABCDEFLL);

    EXPECT_TRUE(err.has_group_tag());
    EXPECT_EQ(err.group_tag(), 0x1234567890ABCDEFLL);
    EXPECT_EQ(err.flags() & ErrorFlyweight::HAS_GROUP_ID_FLAG, ErrorFlyweight::HAS_GROUP_ID_FLAG);
}

TEST(ErrorFlyweight, ErrorMessage)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ErrorFlyweight err{buf};

    std::string msg = "connection refused";
    err.set_error_message(msg);

    EXPECT_EQ(err.error_message_length(), static_cast<i32>(msg.size()));
    EXPECT_EQ(err.error_message(), msg);
    EXPECT_EQ(err.frame_length(), ErrorFlyweight::HEADER_LENGTH + static_cast<i32>(msg.size()));
}

TEST(ErrorFlyweight, ErrorMessageTooLongThrows)
{
    std::array<std::byte, 2048> storage{};
    UnsafeBuffer buf{storage};
    ErrorFlyweight err{buf};

    std::string long_msg(ErrorFlyweight::MAX_ERROR_MESSAGE_LENGTH + 1, 'x');
    EXPECT_THROW(err.set_error_message(long_msg), std::out_of_range);
}

TEST(ErrorFlyweight, WireFormatErrorCodeAtOffset32)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    buf.put_i32(32, 42);
    ErrorFlyweight err{buf};
    EXPECT_EQ(err.error_code(), 42);
}

TEST(ErrorFlyweight, WireFormatSessionIdAtOffset8)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    buf.put_i32(8, 99);
    ErrorFlyweight err{buf};
    EXPECT_EQ(err.session_id(), 99);
}
