#include "caeron/protocol/nak_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(NakFlyweight, HeaderLength)
{
    EXPECT_EQ(NakFlyweight::HEADER_LENGTH, 28);
}

TEST(NakFlyweight, AllFieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    NakFlyweight nak{buf};

    nak.set_frame_length(28)
       .set_version(0)
       .set_flags(0)
       .set_type(HeaderFlyweight::HDR_TYPE_NAK)
       .set_session_id(100)
       .set_stream_id(5)
       .set_term_id(3)
       .set_term_offset(4096)
       .set_length(1024);

    EXPECT_EQ(nak.frame_length(), 28);
    EXPECT_EQ(nak.type(), HeaderFlyweight::HDR_TYPE_NAK);
    EXPECT_EQ(nak.session_id(), 100);
    EXPECT_EQ(nak.stream_id(), 5);
    EXPECT_EQ(nak.term_id(), 3);
    EXPECT_EQ(nak.term_offset(), 4096);
    EXPECT_EQ(nak.length(), 1024);
}

TEST(NakFlyweight, BinaryLayoutCompatibility)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    NakFlyweight nak{buf};

    buf.put_i32(16, 7);    // term_id at offset 16
    buf.put_i32(20, 2048); // term_offset at offset 20
    buf.put_i32(24, 512);  // length at offset 24

    EXPECT_EQ(nak.term_id(), 7);
    EXPECT_EQ(nak.term_offset(), 2048);
    EXPECT_EQ(nak.length(), 512);
}

TEST(NakFlyweight, FieldAccessorsAtNonZeroOffset)
{
    std::array<std::byte, 512> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 128;
    NakFlyweight nak{buf, offset};

    nak.set_frame_length(28)
       .set_version(0)
       .set_flags(0)
       .set_type(HeaderFlyweight::HDR_TYPE_NAK)
       .set_session_id(100)
       .set_stream_id(5)
       .set_term_id(3)
       .set_term_offset(4096)
       .set_length(1024);

    EXPECT_EQ(nak.frame_length(), 28);
    EXPECT_EQ(nak.session_id(), 100);
    EXPECT_EQ(nak.stream_id(), 5);
    EXPECT_EQ(nak.term_id(), 3);
    EXPECT_EQ(nak.term_offset(), 4096);
    EXPECT_EQ(nak.length(), 1024);

    // Verify data is at correct offset, not at offset 0
    EXPECT_EQ(buf.get_i32(offset + 0), 28);
    EXPECT_EQ(buf.get_i32(offset + 8), 100);
    EXPECT_EQ(buf.get_i32(offset + 16), 3);
    EXPECT_EQ(buf.get_i32(offset + 24), 1024);
    EXPECT_EQ(buf.get_i32(0), 0);
}
