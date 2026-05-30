#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(HeaderFlyweight, FieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    HeaderFlyweight hdr{buf};

    hdr.set_frame_length(64).set_version(0).set_flags(0xC0).set_type(HeaderFlyweight::HDR_TYPE_DATA);

    EXPECT_EQ(hdr.frame_length(), 64);
    EXPECT_EQ(hdr.version(), 0);
    EXPECT_EQ(hdr.flags(), 0xC0);
    EXPECT_EQ(hdr.type(), HeaderFlyweight::HDR_TYPE_DATA);
}

TEST(HeaderFlyweight, HeaderLengthConstant)
{
    EXPECT_EQ(HeaderFlyweight::HEADER_LENGTH, 8);
}

TEST(HeaderFlyweight, TypeConstants)
{
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_PAD, 0x00);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_DATA, 0x01);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_NAK, 0x02);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_SM, 0x03);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_ERR, 0x04);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_SETUP, 0x05);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_RTTM, 0x06);
    EXPECT_EQ(HeaderFlyweight::HDR_TYPE_RES, 0x07);
}
