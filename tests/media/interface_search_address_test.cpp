#include "caeron/driver/media/interface_search_address.h"

#include <gtest/gtest.h>

using namespace caeron::driver::media;

TEST(InterfaceSearchAddress, ParseIPv4WithPrefix)
{
    auto addr = InterfaceSearchAddress::parse("192.168.1.0:40456/24");
    EXPECT_EQ(addr.prefix_length(), 24);
    EXPECT_EQ(addr.port(), 40456);
    EXPECT_EQ(addr.address().ss_family, AF_INET);
}

TEST(InterfaceSearchAddress, ParseIPv4WithoutPrefix)
{
    auto addr = InterfaceSearchAddress::parse("192.168.1.0:40456");
    EXPECT_EQ(addr.prefix_length(), 32);  // default full mask
    EXPECT_EQ(addr.port(), 40456);
}

TEST(InterfaceSearchAddress, ParseIPv4WithPrefixNoPort)
{
    auto addr = InterfaceSearchAddress::parse("192.168.1.0/24");
    EXPECT_EQ(addr.prefix_length(), 24);
    EXPECT_EQ(addr.port(), 0);
}

TEST(InterfaceSearchAddress, ParseIPv6WithPrefix)
{
    auto addr = InterfaceSearchAddress::parse("[::1]:8080/64");
    EXPECT_EQ(addr.prefix_length(), 64);
    EXPECT_EQ(addr.port(), 8080);
    EXPECT_EQ(addr.address().ss_family, AF_INET6);
}

TEST(InterfaceSearchAddress, ParseIPv6WithoutPrefix)
{
    auto addr = InterfaceSearchAddress::parse("[::1]:8080");
    EXPECT_EQ(addr.prefix_length(), 128);  // default full mask for IPv6
    EXPECT_EQ(addr.port(), 8080);
}

TEST(InterfaceSearchAddress, Wildcard)
{
    auto addr = InterfaceSearchAddress::wildcard();
    EXPECT_EQ(addr.prefix_length(), 0);
    EXPECT_EQ(addr.port(), 0);
    EXPECT_EQ(addr.address().ss_family, AF_INET);
}

TEST(InterfaceSearchAddress, ParseInvalidPrefixThrows)
{
    auto fn = []() { (void)InterfaceSearchAddress::parse("192.168.1.0/abc"); };
    EXPECT_THROW(fn(), std::invalid_argument);
}

TEST(InterfaceSearchAddress, ParsePrefixOutOfRangeThrows)
{
    auto fn = []() { (void)InterfaceSearchAddress::parse("192.168.1.0/200"); };
    EXPECT_THROW(fn(), std::invalid_argument);
}

// MEDIUM-3: Verify that very long numeric strings don't cause signed overflow UB
TEST(InterfaceSearchAddress, ParsePrefixOverflowThrows)
{
    // "999999999999" would overflow int during accumulation.
    // With the fix (unsigned accumulation + early range check), this must throw.
    auto fn = []() { (void)InterfaceSearchAddress::parse("192.168.1.0/999999999999"); };
    EXPECT_THROW(fn(), std::invalid_argument);
}

// Verify boundary values
TEST(InterfaceSearchAddress, ParsePrefixBoundary)
{
    EXPECT_EQ(InterfaceSearchAddress::parse("192.168.1.0/0").prefix_length(), 0);
    // IPv4 max prefix is 32; /128 on IPv4 must be rejected
    {
        auto fn = []() { (void)InterfaceSearchAddress::parse("192.168.1.0/128"); };
        EXPECT_THROW(fn(), std::invalid_argument);
    }
    // IPv6 /128 is valid
    EXPECT_EQ(InterfaceSearchAddress::parse("[::1]:0/128").prefix_length(), 128);
    // Global max is still 128
    {
        auto fn = []() { (void)InterfaceSearchAddress::parse("192.168.1.0/129"); };
        EXPECT_THROW(fn(), std::invalid_argument);
    }
}

// Finding 4: IPv6 bracket notation without port should be accepted
TEST(InterfaceSearchAddress, ParseIPv6WithoutPort)
{
    // [::1]/128 — no port, should default to port 0
    auto addr = InterfaceSearchAddress::parse("[::1]/128");
    EXPECT_EQ(addr.prefix_length(), 128);
    EXPECT_EQ(addr.port(), 0);
    EXPECT_EQ(addr.address().ss_family, AF_INET6);

    // [::1] — no port, no prefix, should default to port 0 and prefix 128
    auto addr2 = InterfaceSearchAddress::parse("[::1]");
    EXPECT_EQ(addr2.prefix_length(), 128);
    EXPECT_EQ(addr2.port(), 0);
    EXPECT_EQ(addr2.address().ss_family, AF_INET6);

    // [::1]:40456/64 — with port and prefix
    auto addr3 = InterfaceSearchAddress::parse("[::1]:40456/64");
    EXPECT_EQ(addr3.prefix_length(), 64);
    EXPECT_EQ(addr3.port(), 40456);
    EXPECT_EQ(addr3.address().ss_family, AF_INET6);
}
