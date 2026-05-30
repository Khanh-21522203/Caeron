#include "caeron/protocol/status_message_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(StatusMessageFlyweight, HeaderLength)
{
    EXPECT_EQ(StatusMessageFlyweight::HEADER_LENGTH, 36);
    EXPECT_EQ(StatusMessageFlyweight::HEADER_LENGTH_WITH_GROUP_TAG, 44);
}

TEST(StatusMessageFlyweight, AllFieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    StatusMessageFlyweight sm{buf};

    sm.set_frame_length(36)
      .set_version(0)
      .set_flags(StatusMessageFlyweight::SEND_SETUP_FLAG)
      .set_type(HeaderFlyweight::HDR_TYPE_SM)
      .set_session_id(0xAABBCCDD)
      .set_stream_id(42)
      .set_consumption_term_id(3)
      .set_consumption_term_offset(1024)
      .set_receiver_window(131072)
      .set_receiver_id(0x1122334455667788LL);

    EXPECT_EQ(sm.frame_length(), 36);
    EXPECT_EQ(sm.type(), HeaderFlyweight::HDR_TYPE_SM);
    EXPECT_EQ(sm.session_id(), static_cast<i32>(0xAABBCCDD));
    EXPECT_EQ(sm.stream_id(), 42);
    EXPECT_EQ(sm.consumption_term_id(), 3);
    EXPECT_EQ(sm.consumption_term_offset(), 1024);
    EXPECT_EQ(sm.receiver_window(), 131072);
    EXPECT_EQ(sm.receiver_id(), 0x1122334455667788LL);
}

TEST(StatusMessageFlyweight, GroupTag)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    StatusMessageFlyweight sm{buf};

    sm.set_group_tag(0xCAFEBABE);
    EXPECT_EQ(sm.group_tag(), 0xCAFEBABE);
}
