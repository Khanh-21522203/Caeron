#include "caeron/driver/media/network_util.h"
#include "caeron/driver/media/socket_address_parser.h"

#include <gtest/gtest.h>

using namespace caeron;
using namespace caeron::driver::media;

// --- is_match_with_prefix ---

TEST(NetworkUtil, MatchWithPrefixIPv4FullMatch)
{
    u8 a[] = {192, 168, 1, 100};
    u8 b[] = {192, 168, 1, 100};
    EXPECT_TRUE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 4, 32));
}

TEST(NetworkUtil, MatchWithPrefixIPv4Zero)
{
    u8 a[] = {192, 168, 1, 100};
    u8 b[] = {10, 0, 0, 1};
    EXPECT_TRUE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 4, 0));
}

TEST(NetworkUtil, MatchWithPrefixIPv4Partial)
{
    u8 a[] = {192, 168, 1, 100};
    u8 b[] = {192, 168, 2, 1};
    EXPECT_FALSE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 4, 24));
}

TEST(NetworkUtil, MatchWithPrefixIPv4Partial2)
{
    u8 a[] = {192, 168, 1, 100};
    u8 b[] = {192, 168, 1, 1};
    EXPECT_TRUE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 4, 24));
}

TEST(NetworkUtil, MatchWithPrefixIPv4MidByte)
{
    u8 a[] = {192, 168, 0b11000000, 0};
    u8 b[] = {192, 168, 0b11001111, 0};
    EXPECT_TRUE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 4, 18));
}

TEST(NetworkUtil, MatchWithPrefixIPv6)
{
    u8 a[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    u8 b[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    EXPECT_TRUE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 16, 64));
}

TEST(NetworkUtil, MatchWithPrefixIPv6NoMatch)
{
    u8 a[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    u8 b[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    EXPECT_FALSE(network_util::is_match_with_prefix(
        reinterpret_cast<const std::byte*>(a),
        reinterpret_cast<const std::byte*>(b), 16, 64));
}

// --- prefix_length_to_ipv4_mask ---

TEST(NetworkUtil, PrefixLengthToMask)
{
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(0), 0x00000000u);
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(8), 0xFF000000u);
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(16), 0xFFFF0000u);
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(24), 0xFFFFFF00u);
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(32), 0xFFFFFFFFu);
}

TEST(NetworkUtil, PrefixLengthToMaskEdge)
{
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(1), 0x80000000u);
    EXPECT_EQ(network_util::prefix_length_to_ipv4_mask(31), 0xFFFFFFFEu);
}

// --- get_network_interfaces ---

TEST(NetworkUtil, GetNetworkInterfaces)
{
    auto interfaces = network_util::get_network_interfaces();
    EXPECT_FALSE(interfaces.empty());

    bool has_loopback = false;
    for (const auto& iface : interfaces)
    {
        if (iface.is_loopback)
        {
            has_loopback = true;
            break;
        }
    }
    EXPECT_TRUE(has_loopback);
}

// --- filter_by_subnet ---

TEST(NetworkUtil, FilterBySubnet)
{
    auto addr = socket_address_parser::resolve_host("127.0.0.1", 0);
    auto matches = network_util::filter_by_subnet(addr, 8);

    EXPECT_FALSE(matches.empty());
    for (const auto& m : matches)
    {
        EXPECT_EQ(m.addr.ss_family, AF_INET);
        const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(&m.addr);
        EXPECT_EQ(reinterpret_cast<const u8*>(&a4->sin_addr.s_addr)[0], 127);
    }
}

// --- find_first_matching_local_address ---

TEST(NetworkUtil, FindFirstMatchingLocalAddress)
{
    auto addr = socket_address_parser::resolve_host("127.0.0.1", 0);
    auto result = network_util::find_first_matching_local_address(addr, 8);
    EXPECT_TRUE(result.has_value());
}
