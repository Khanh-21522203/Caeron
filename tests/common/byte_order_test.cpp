#include "caeron/common/byte_order.h"

#include <gtest/gtest.h>

using namespace caeron;

TEST(ByteOrder, RoundTripU16)
{
    constexpr u16 val = 0x1234;
    EXPECT_EQ(from_le(to_le(val)), val);
}

TEST(ByteOrder, RoundTripU32)
{
    constexpr u32 val = 0xDEADBEEF;
    EXPECT_EQ(from_le(to_le(val)), val);
}

TEST(ByteOrder, RoundTripU64)
{
    constexpr u64 val = 0x0102030405060708ULL;
    EXPECT_EQ(from_le(to_le(val)), val);
}

TEST(ByteOrder, RoundTripI32)
{
    constexpr i32 val = -42;
    EXPECT_EQ(from_le(to_le(val)), val);
}

TEST(ByteOrder, RoundTripI64)
{
    constexpr i64 val = -1234567890LL;
    EXPECT_EQ(from_le(to_le(val)), val);
}
