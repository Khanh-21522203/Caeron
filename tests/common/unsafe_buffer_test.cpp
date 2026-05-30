#include "caeron/concurrent/unsafe_buffer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

using namespace caeron;
using namespace caeron::concurrent;

class UnsafeBufferTest : public ::testing::Test
{
protected:
    static constexpr i32 BUFFER_SIZE = 256;
    std::array<std::byte, BUFFER_SIZE> storage_{};
    UnsafeBuffer buf_{storage_};
};

TEST_F(UnsafeBufferTest, InitialState)
{
    EXPECT_EQ(buf_.capacity(), BUFFER_SIZE);
    EXPECT_NE(buf_.data(), nullptr);
}

TEST_F(UnsafeBufferTest, PutAndGetU8)
{
    buf_.put_u8(0, 0xAB);
    EXPECT_EQ(buf_.get_u8(0), 0xAB);

    buf_.put_u8(100, 0xFF);
    EXPECT_EQ(buf_.get_u8(100), 0xFF);
}

TEST_F(UnsafeBufferTest, PutAndGetI8)
{
    buf_.put_i8(0, -42);
    EXPECT_EQ(buf_.get_i8(0), -42);
}

TEST_F(UnsafeBufferTest, PutAndGetU16)
{
    buf_.put_u16(0, 0x1234);
    EXPECT_EQ(buf_.get_u16(0), 0x1234);
}

TEST_F(UnsafeBufferTest, PutAndGetI16)
{
    buf_.put_i16(0, -1234);
    EXPECT_EQ(buf_.get_i16(0), -1234);
}

TEST_F(UnsafeBufferTest, PutAndGetU32)
{
    buf_.put_u32(0, 0xDEADBEEF);
    EXPECT_EQ(buf_.get_u32(0), 0xDEADBEEF);
}

TEST_F(UnsafeBufferTest, PutAndGetI32)
{
    buf_.put_i32(0, -123456);
    EXPECT_EQ(buf_.get_i32(0), -123456);
}

TEST_F(UnsafeBufferTest, PutAndGetU64)
{
    buf_.put_u64(0, 0x0102030405060708ULL);
    EXPECT_EQ(buf_.get_u64(0), 0x0102030405060708ULL);
}

TEST_F(UnsafeBufferTest, PutAndGetI64)
{
    buf_.put_i64(0, -9876543210LL);
    EXPECT_EQ(buf_.get_i64(0), -9876543210LL);
}

TEST_F(UnsafeBufferTest, VolatileI32)
{
    buf_.put_i32_volatile(0, 42);
    EXPECT_EQ(buf_.get_i32_volatile(0), 42);
}

TEST_F(UnsafeBufferTest, VolatileI64)
{
    buf_.put_i64_volatile(0, 123456789LL);
    EXPECT_EQ(buf_.get_i64_volatile(0), 123456789LL);
}

TEST_F(UnsafeBufferTest, OrderedI32)
{
    buf_.put_i32_ordered(0, 99);
    EXPECT_EQ(buf_.get_i32_ordered(0), 99);
}

TEST_F(UnsafeBufferTest, OrderedI64)
{
    buf_.put_i64_ordered(0, 9876543210LL);
    EXPECT_EQ(buf_.get_i64_ordered(0), 9876543210LL);
}

TEST_F(UnsafeBufferTest, CompareAndSetI32)
{
    buf_.put_i32(0, 10);
    EXPECT_TRUE(buf_.compare_and_set_i32(0, 10, 20));
    EXPECT_EQ(buf_.get_i32(0), 20);

    EXPECT_FALSE(buf_.compare_and_set_i32(0, 10, 30));
    EXPECT_EQ(buf_.get_i32(0), 20);
}

TEST_F(UnsafeBufferTest, CompareAndSetI64)
{
    buf_.put_i64(0, 100LL);
    EXPECT_TRUE(buf_.compare_and_set_i64(0, 100LL, 200LL));
    EXPECT_EQ(buf_.get_i64(0), 200LL);

    EXPECT_FALSE(buf_.compare_and_set_i64(0, 100LL, 300LL));
    EXPECT_EQ(buf_.get_i64(0), 200LL);
}

TEST_F(UnsafeBufferTest, GetAndAddI64)
{
    buf_.put_i64(0, 10LL);
    auto prev = buf_.get_and_add_i64(0, 5LL);
    EXPECT_EQ(prev, 10LL);
    EXPECT_EQ(buf_.get_i64(0), 15LL);
}

TEST_F(UnsafeBufferTest, PutAndGetBytes)
{
    const char data[] = "hello";
    buf_.put_bytes(10, data, 5);

    char out[5]{};
    buf_.get_bytes(10, out, 5);
    EXPECT_EQ(std::memcmp(out, data, 5), 0);
}

TEST_F(UnsafeBufferTest, SetMemory)
{
    buf_.set_memory(0, 10, 0xFF);
    for (i32 i = 0; i < 10; ++i)
        EXPECT_EQ(buf_.get_u8(i), 0xFF);
}
