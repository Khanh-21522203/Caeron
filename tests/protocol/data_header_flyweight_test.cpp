#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(DataHeaderFlyweight, HeaderLength)
{
    EXPECT_EQ(DataHeaderFlyweight::HEADER_LENGTH, 32);
}

TEST(DataHeaderFlyweight, AllFieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    DataHeaderFlyweight hdr{buf};

    hdr.set_frame_length(32)
       .set_version(0)
       .set_flags(DataHeaderFlyweight::UNFRAGMENTED)
       .set_type(HeaderFlyweight::HDR_TYPE_DATA)
       .set_term_offset(1024)
       .set_session_id(0x12345678)
       .set_stream_id(42)
       .set_term_id(7)
       .set_reserved_value(0xDEADBEEFCAFE0001LL);

    EXPECT_EQ(hdr.frame_length(), 32);
    EXPECT_EQ(hdr.version(), 0);
    EXPECT_EQ(hdr.flags(), DataHeaderFlyweight::UNFRAGMENTED);
    EXPECT_EQ(hdr.type(), HeaderFlyweight::HDR_TYPE_DATA);
    EXPECT_EQ(hdr.term_offset(), 1024);
    EXPECT_EQ(hdr.session_id(), 0x12345678);
    EXPECT_EQ(hdr.stream_id(), 42);
    EXPECT_EQ(hdr.term_id(), 7);
    EXPECT_EQ(hdr.reserved_value(), 0xDEADBEEFCAFE0001LL);
}

TEST(DataHeaderFlyweight, BinaryLayoutCompatibility)
{
    // Verify field offsets match Java Aeron exactly
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    DataHeaderFlyweight hdr{buf};

    // Write raw bytes at known offsets and verify accessors read them
    buf.put_i32(0, 100);   // frame_length at offset 0
    buf.put_u8(4, 1);      // version at offset 4
    buf.put_u8(5, 0xC0);   // flags at offset 5
    buf.put_u16(6, 0x01);  // type at offset 6
    buf.put_i32(8, 2048);  // term_offset at offset 8
    buf.put_i32(12, 99);   // session_id at offset 12
    buf.put_i32(16, 5);    // stream_id at offset 16
    buf.put_i32(20, 3);    // term_id at offset 20
    buf.put_i64(24, 42);   // reserved_value at offset 24

    EXPECT_EQ(hdr.frame_length(), 100);
    EXPECT_EQ(hdr.session_id(), 99);
    EXPECT_EQ(hdr.stream_id(), 5);
    EXPECT_EQ(hdr.term_id(), 3);
    EXPECT_EQ(hdr.term_offset(), 2048);
    EXPECT_EQ(hdr.reserved_value(), 42);
}

TEST(DataHeaderFlyweight, FieldAccessorsAtNonZeroOffset)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 64;
    DataHeaderFlyweight hdr{buf, offset};

    hdr.set_frame_length(32)
       .set_version(0)
       .set_flags(DataHeaderFlyweight::UNFRAGMENTED)
       .set_type(HeaderFlyweight::HDR_TYPE_DATA)
       .set_term_offset(4096)
       .set_session_id(0xAAAAAAAA)
       .set_stream_id(99)
       .set_term_id(42)
       .set_reserved_value(0x1122334455667788LL);

    EXPECT_EQ(hdr.frame_length(), 32);
    EXPECT_EQ(hdr.flags(), DataHeaderFlyweight::UNFRAGMENTED);
    EXPECT_EQ(hdr.term_offset(), 4096);
    EXPECT_EQ(hdr.session_id(), 0xAAAAAAAA);
    EXPECT_EQ(hdr.stream_id(), 99);
    EXPECT_EQ(hdr.term_id(), 42);
    EXPECT_EQ(hdr.reserved_value(), 0x1122334455667788LL);

    // Verify data is at correct offset, not at offset 0
    EXPECT_EQ(buf.get_i32(offset + 0), 32);
    EXPECT_EQ(buf.get_i32(offset + 12), 0xAAAAAAAA);
    EXPECT_EQ(buf.get_i32(offset + 20), 42);
}

TEST(DataHeaderFlyweight, FlagConstants)
{
    EXPECT_EQ(DataHeaderFlyweight::BEGIN_FLAG, 0x80);
    EXPECT_EQ(DataHeaderFlyweight::END_FLAG, 0x40);
    EXPECT_EQ(DataHeaderFlyweight::EOS_FLAG, 0x20);
    EXPECT_EQ(DataHeaderFlyweight::REVOKED_FLAG, 0x10);
    EXPECT_EQ(DataHeaderFlyweight::UNFRAGMENTED, 0xC0);
    EXPECT_EQ(DataHeaderFlyweight::BEGIN_AND_END_FLAGS, 0xC0);
    EXPECT_EQ(DataHeaderFlyweight::BEGIN_END_AND_EOS_FLAGS, 0xE0);
    EXPECT_EQ(DataHeaderFlyweight::BEGIN_END_EOS_AND_REVOKED_FLAGS, 0xF0);
}

TEST(DataHeaderFlyweight, IsEndOfStreamAndIsRevoked)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};

    // No flags set
    EXPECT_FALSE(DataHeaderFlyweight::is_end_of_stream(buf));
    EXPECT_FALSE(DataHeaderFlyweight::is_revoked(buf));

    // Set EOS flag
    buf.put_u8(5, DataHeaderFlyweight::EOS_FLAG);
    EXPECT_TRUE(DataHeaderFlyweight::is_end_of_stream(buf));
    EXPECT_FALSE(DataHeaderFlyweight::is_revoked(buf));

    // Set REVOKED flag
    buf.put_u8(5, DataHeaderFlyweight::REVOKED_FLAG);
    EXPECT_FALSE(DataHeaderFlyweight::is_end_of_stream(buf));
    EXPECT_TRUE(DataHeaderFlyweight::is_revoked(buf));

    // Set both
    buf.put_u8(5, DataHeaderFlyweight::EOS_FLAG | DataHeaderFlyweight::REVOKED_FLAG);
    EXPECT_TRUE(DataHeaderFlyweight::is_end_of_stream(buf));
    EXPECT_TRUE(DataHeaderFlyweight::is_revoked(buf));
}

TEST(DataHeaderFlyweight, IsEndOfStreamAtNonZeroOffset)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 128;

    // Set EOS flag at non-zero offset
    buf.put_u8(offset + 5, DataHeaderFlyweight::EOS_FLAG);
    EXPECT_TRUE(DataHeaderFlyweight::is_end_of_stream(buf, offset));
    EXPECT_FALSE(DataHeaderFlyweight::is_revoked(buf, offset));

    // Verify offset 0 is unaffected
    EXPECT_FALSE(DataHeaderFlyweight::is_end_of_stream(buf, 0));
}
