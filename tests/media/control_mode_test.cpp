#include "caeron/driver/media/control_mode.h"

#include <gtest/gtest.h>

using namespace caeron::driver::media;

TEST(ControlMode, ParseDynamic)
{
    EXPECT_EQ(parse_control_mode("dynamic"), ControlMode::DYNAMIC);
}

TEST(ControlMode, ParseManual)
{
    EXPECT_EQ(parse_control_mode("manual"), ControlMode::MANUAL);
}

TEST(ControlMode, ParseResponse)
{
    EXPECT_EQ(parse_control_mode("response"), ControlMode::RESPONSE);
}

TEST(ControlMode, ParseNone)
{
    EXPECT_EQ(parse_control_mode("none"), ControlMode::NONE);
}

TEST(ControlMode, ParseEmpty)
{
    EXPECT_EQ(parse_control_mode(""), ControlMode::NONE);
}

TEST(ControlMode, ParseUnknownThrows)
{
    auto fn = []() { (void)parse_control_mode("unknown"); };
    EXPECT_THROW(fn(), std::invalid_argument);
}

TEST(ControlMode, IsMultiDestination)
{
    EXPECT_TRUE(is_multi_destination(ControlMode::DYNAMIC));
    EXPECT_TRUE(is_multi_destination(ControlMode::MANUAL));
    EXPECT_FALSE(is_multi_destination(ControlMode::NONE));
    EXPECT_FALSE(is_multi_destination(ControlMode::RESPONSE));
}
