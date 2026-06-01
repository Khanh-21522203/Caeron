#include "caeron/driver/media/socket_address_parser.h"

#include <gtest/gtest.h>

using namespace caeron;
using namespace caeron::driver::media;

// --- parse() tests ---

TEST(SocketAddressParser, ParseIPv4)
{
    auto result = socket_address_parser::parse("192.168.1.1:40456");
    EXPECT_EQ(result.host, "192.168.1.1");
    EXPECT_EQ(result.port, 40456);
}

TEST(SocketAddressParser, ParseIPv4Localhost)
{
    auto result = socket_address_parser::parse("127.0.0.1:0");
    EXPECT_EQ(result.host, "127.0.0.1");
    EXPECT_EQ(result.port, 0);
}

TEST(SocketAddressParser, ParseIPv6Bracket)
{
    auto result = socket_address_parser::parse("[::1]:1234");
    EXPECT_EQ(result.host, "::1");
    EXPECT_EQ(result.port, 1234);
}

TEST(SocketAddressParser, ParseIPv6WithScope)
{
    auto result = socket_address_parser::parse("[fe80::1%eth0]:5000");
    EXPECT_EQ(result.host, "fe80::1%eth0");
    EXPECT_EQ(result.port, 5000);
}

TEST(SocketAddressParser, ParseIPv6Full)
{
    auto result = socket_address_parser::parse("[2001:db8::1]:8080");
    EXPECT_EQ(result.host, "2001:db8::1");
    EXPECT_EQ(result.port, 8080);
}

TEST(SocketAddressParser, ParsePortZero)
{
    auto result = socket_address_parser::parse("0.0.0.0:0");
    EXPECT_EQ(result.port, 0);
}

TEST(SocketAddressParser, ParsePortMax)
{
    auto result = socket_address_parser::parse("0.0.0.0:65535");
    EXPECT_EQ(result.port, 65535);
}

TEST(SocketAddressParser, ParseEmptyThrows)
{
    EXPECT_THROW(socket_address_parser::parse(""), std::invalid_argument);
}

TEST(SocketAddressParser, ParseMissingPortThrows)
{
    EXPECT_THROW(socket_address_parser::parse("192.168.1.1"), std::invalid_argument);
}

TEST(SocketAddressParser, ParseEmptyPortThrows)
{
    EXPECT_THROW(socket_address_parser::parse("192.168.1.1:"), std::invalid_argument);
}

TEST(SocketAddressParser, ParseNonNumericPortThrows)
{
    EXPECT_THROW(socket_address_parser::parse("192.168.1.1:abc"), std::invalid_argument);
}

TEST(SocketAddressParser, ParsePortOutOfRangeThrows)
{
    EXPECT_THROW(socket_address_parser::parse("192.168.1.1:70000"), std::invalid_argument);
}

TEST(SocketAddressParser, ParseIPv6MissingBracketThrows)
{
    EXPECT_THROW(socket_address_parser::parse("[::1:1234"), std::invalid_argument);
}

TEST(SocketAddressParser, ParseIPv6MissingPortThrows)
{
    EXPECT_THROW(socket_address_parser::parse("[::1]"), std::invalid_argument);
}

TEST(SocketAddressParser, ParseIPv6EmptyHostThrows)
{
    EXPECT_THROW(socket_address_parser::parse("[]:1234"), std::invalid_argument);
}

// --- parse_port() tests ---

TEST(SocketAddressParser, ParsePortValid)
{
    EXPECT_EQ(socket_address_parser::parse_port("80"), 80);
    EXPECT_EQ(socket_address_parser::parse_port("0"), 0);
    EXPECT_EQ(socket_address_parser::parse_port("65535"), 65535);
}

TEST(SocketAddressParser, ParsePortInvalid)
{
    EXPECT_THROW(socket_address_parser::parse_port(""), std::invalid_argument);
    EXPECT_THROW(socket_address_parser::parse_port("-1"), std::invalid_argument);
    EXPECT_THROW(socket_address_parser::parse_port("abc"), std::invalid_argument);
    EXPECT_THROW(socket_address_parser::parse_port("65536"), std::invalid_argument);
}

// --- is_multicast_address() tests ---

TEST(SocketAddressParser, IsMulticastIPv4)
{
    EXPECT_TRUE(socket_address_parser::is_multicast_address("224.0.0.1:1234"));
    EXPECT_TRUE(socket_address_parser::is_multicast_address("239.255.255.255:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("192.168.1.1:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("127.0.0.1:1234"));
}

TEST(SocketAddressParser, IsMulticastIPv6)
{
    EXPECT_TRUE(socket_address_parser::is_multicast_address("[ff02::1]:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("[::1]:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("[fe80::1]:1234"));
}

// Finding 5: Scoped IPv6 multicast — scope ID must be stripped before inet_pton
TEST(SocketAddressParser, IsMulticastIPv6Scoped)
{
    EXPECT_TRUE(socket_address_parser::is_multicast_address("[ff02::1%eth0]:1234"));
    EXPECT_TRUE(socket_address_parser::is_multicast_address("[ff02::1%1]:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("[fe80::1%eth0]:1234"));
    EXPECT_FALSE(socket_address_parser::is_multicast_address("[::1%lo]:1234"));
}

// --- resolve_host() tests ---

TEST(SocketAddressParser, ResolveHostIPv4)
{
    auto addr = socket_address_parser::resolve_host("192.168.1.1", 8080);
    EXPECT_EQ(addr.ss_family, AF_INET);
    EXPECT_EQ(socket_address_parser::get_port(addr), 8080);
}

TEST(SocketAddressParser, ResolveHostIPv6)
{
    auto addr = socket_address_parser::resolve_host("::1", 9090);
    EXPECT_EQ(addr.ss_family, AF_INET6);
    EXPECT_EQ(socket_address_parser::get_port(addr), 9090);
}

TEST(SocketAddressParser, ResolveHostInvalidThrows)
{
    EXPECT_THROW(socket_address_parser::resolve_host("not_an_ip", 80),
                 std::invalid_argument);
}

// --- get_port / set_port ---

TEST(SocketAddressParser, GetSetPortIPv4)
{
    struct sockaddr_storage addr{};
    auto* a4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    a4->sin_family = AF_INET;

    socket_address_parser::set_port(addr, 1234);
    EXPECT_EQ(socket_address_parser::get_port(addr), 1234);
}

TEST(SocketAddressParser, GetSetPortIPv6)
{
    struct sockaddr_storage addr{};
    auto* a6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
    a6->sin6_family = AF_INET6;

    socket_address_parser::set_port(addr, 5678);
    EXPECT_EQ(socket_address_parser::get_port(addr), 5678);
}

// --- format_address_and_port ---

TEST(SocketAddressParser, FormatIPv4)
{
    auto addr = socket_address_parser::resolve_host("10.0.0.1", 40456);
    EXPECT_EQ(socket_address_parser::format_address_and_port(addr), "10.0.0.1:40456");
}

TEST(SocketAddressParser, FormatIPv6)
{
    auto addr = socket_address_parser::resolve_host("::1", 8080);
    EXPECT_EQ(socket_address_parser::format_address_and_port(addr), "[::1]:8080");
}

// --- is_any_address ---

TEST(SocketAddressParser, IsAnyAddressIPv4)
{
    auto addr = socket_address_parser::resolve_host("0.0.0.0", 0);
    EXPECT_TRUE(socket_address_parser::is_any_address(addr));
}

TEST(SocketAddressParser, IsNotAnyAddress)
{
    auto addr = socket_address_parser::resolve_host("127.0.0.1", 0);
    EXPECT_FALSE(socket_address_parser::is_any_address(addr));
}

// --- addresses_equal ---

TEST(SocketAddressParser, AddressesEqual)
{
    auto a = socket_address_parser::resolve_host("10.0.0.1", 1000);
    auto b = socket_address_parser::resolve_host("10.0.0.1", 2000);
    EXPECT_TRUE(socket_address_parser::addresses_equal(a, b));
    EXPECT_FALSE(socket_address_parser::addresses_and_ports_equal(a, b));
}

TEST(SocketAddressParser, AddressesNotEqual)
{
    auto a = socket_address_parser::resolve_host("10.0.0.1", 1000);
    auto b = socket_address_parser::resolve_host("10.0.0.2", 1000);
    EXPECT_FALSE(socket_address_parser::addresses_equal(a, b));
}

// Finding 2: Scoped IPv6 with numeric scope ID
TEST(SocketAddressParser, ResolveHostIPv6NumericScope)
{
    auto addr = socket_address_parser::resolve_host("fe80::1%1", 5000);
    EXPECT_EQ(addr.ss_family, AF_INET6);
    EXPECT_EQ(socket_address_parser::get_port(addr), 5000);
    auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
    EXPECT_EQ(a6->sin6_scope_id, 1u);
}

// Finding 2: Scoped IPv6 with invalid interface name must throw
TEST(SocketAddressParser, ResolveHostIPv6InvalidScopeNameThrows)
{
    EXPECT_THROW(
        socket_address_parser::resolve_host("fe80::1%nonexistent_iface_xyz", 5000),
        std::invalid_argument);
}

// Finding 10: IPv6 scope ID included in format
TEST(SocketAddressParser, FormatIPv6WithScopeId)
{
    auto addr = socket_address_parser::resolve_host("fe80::1%1", 5000);
    auto formatted = socket_address_parser::format_address_and_port(addr);
    EXPECT_NE(formatted.find("%1"), std::string::npos);
}

// Finding 10: IPv6 scope ID included in equality comparison
TEST(SocketAddressParser, AddressesEqualIPv6ScopeIdDiffers)
{
    auto a = socket_address_parser::resolve_host("fe80::1%1", 5000);
    auto b = socket_address_parser::resolve_host("fe80::1%2", 5000);
    EXPECT_FALSE(socket_address_parser::addresses_equal(a, b));
}

// Finding 10: IPv6 same scope ID is equal
TEST(SocketAddressParser, AddressesEqualIPv6SameScopeId)
{
    auto a = socket_address_parser::resolve_host("fe80::1%1", 5000);
    auto b = socket_address_parser::resolve_host("fe80::1%1", 6000);
    EXPECT_TRUE(socket_address_parser::addresses_equal(a, b));
}
