#include "caeron/common/bit_util.h"

#include <gtest/gtest.h>

using namespace caeron;

TEST(BitUtil, IsPowerOfTwo)
{
    EXPECT_TRUE(is_power_of_two(1));
    EXPECT_TRUE(is_power_of_two(2));
    EXPECT_TRUE(is_power_of_two(4));
    EXPECT_TRUE(is_power_of_two(64));
    EXPECT_TRUE(is_power_of_two(1024));
    EXPECT_FALSE(is_power_of_two(0));
    EXPECT_FALSE(is_power_of_two(3));
    EXPECT_FALSE(is_power_of_two(5));
    EXPECT_FALSE(is_power_of_two(63));
}

TEST(BitUtil, Align)
{
    EXPECT_EQ(align(0, 4), 0);
    EXPECT_EQ(align(1, 4), 4);
    EXPECT_EQ(align(4, 4), 4);
    EXPECT_EQ(align(5, 4), 8);
    EXPECT_EQ(align(7, 8), 8);
    EXPECT_EQ(align(9, 8), 16);
    EXPECT_EQ(align(63, 64), 64);
    EXPECT_EQ(align(64, 64), 64);
    EXPECT_EQ(align(65, 64), 128);
}

TEST(BitUtil, AlignInvalidAlignment)
{
    EXPECT_THROW((void)align(1, 3), std::invalid_argument);
}

TEST(BitUtil, NextPowerOfTwo)
{
    EXPECT_EQ(next_power_of_two(1), 1);
    EXPECT_EQ(next_power_of_two(2), 2);
    EXPECT_EQ(next_power_of_two(3), 4);
    EXPECT_EQ(next_power_of_two(5), 8);
    EXPECT_EQ(next_power_of_two(64), 64);
    EXPECT_EQ(next_power_of_two(65), 128);
}
