#include "caeron/driver/media/named_interface.h"

#include <gtest/gtest.h>

using namespace caeron::driver::media;

TEST(NamedInterface, ParseWithNameAndPort)
{
    auto ni = NamedInterface::parse("{lo}:40456");
    EXPECT_EQ(ni.name(), "lo");
    EXPECT_EQ(ni.port(), 40456);
}

TEST(NamedInterface, ParseWithNameOnly)
{
    auto ni = NamedInterface::parse("{lo}");
    EXPECT_EQ(ni.name(), "lo");
    EXPECT_EQ(ni.port(), 0);
}

TEST(NamedInterface, ParseMissingOpeningBraceThrows)
{
    EXPECT_THROW(NamedInterface::parse("lo}"), std::invalid_argument);
}

TEST(NamedInterface, ParseMissingClosingBraceThrows)
{
    EXPECT_THROW(NamedInterface::parse("{lo"), std::invalid_argument);
}

TEST(NamedInterface, ParseEmptyNameThrows)
{
    EXPECT_THROW(NamedInterface::parse("{}"), std::invalid_argument);
}

TEST(NamedInterface, ParseInvalidPortThrows)
{
    EXPECT_THROW(NamedInterface::parse("{lo}:abc"), std::invalid_argument);
}

TEST(NamedInterface, ResolveLoopbackIPv4)
{
    auto ni = NamedInterface::parse("{lo}:1234");
    // Loopback should resolve on any Linux system
    auto resolved = ni.resolve(false, AF_INET);
    EXPECT_TRUE(resolved.has_interface);
    EXPECT_GT(resolved.interface_index, 0);
    EXPECT_EQ(resolved.interface_name, "lo");
    EXPECT_EQ(resolved.address.ss_family, AF_INET);
}

TEST(NamedInterface, ResolveNonexistentThrows)
{
    auto ni = NamedInterface::parse("{nonexistent_if}:1234");
    EXPECT_THROW(ni.resolve(false, AF_INET), std::runtime_error);
}
