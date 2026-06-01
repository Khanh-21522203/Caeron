#include "caeron/driver/media/udp_channel.h"

#include <gtest/gtest.h>
#include <limits>

using namespace caeron;
using namespace caeron::driver::media;

TEST(UdpChannel, ParseUnicastEndpoint)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    EXPECT_TRUE(ch.has_explicit_endpoint());
    EXPECT_FALSE(ch.is_multicast());
    EXPECT_EQ(ch.protocol_family(), AF_INET);
    EXPECT_EQ(ch.control_mode(), ControlMode::NONE);
    EXPECT_FALSE(ch.canonical_form().empty());
}

TEST(UdpChannel, ParseMulticastEndpoint)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=224.0.1.1:40456");
    EXPECT_TRUE(ch.has_explicit_endpoint());
    EXPECT_TRUE(ch.is_multicast());
    EXPECT_TRUE(ch.has_explicit_endpoint());
}

TEST(UdpChannel, ParseMulticastWithTTL)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=224.0.1.1:40456|multicast-ttl=4");
    EXPECT_TRUE(ch.is_multicast());
    EXPECT_TRUE(ch.has_multicast_ttl());
    EXPECT_EQ(ch.multicast_ttl(), 4);
}

TEST(UdpChannel, ParseDynamicControlMode)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=224.0.1.1:40456|control-mode=dynamic|control=224.0.1.1:40457");
    EXPECT_TRUE(ch.is_dynamic_control_mode());
    EXPECT_TRUE(ch.is_multi_destination());
    EXPECT_TRUE(ch.has_explicit_control());
}

TEST(UdpChannel, ParseManualControlMode)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=224.0.1.1:40456|control-mode=manual|control=224.0.1.1:40457");
    EXPECT_TRUE(ch.is_manual_control_mode());
    EXPECT_TRUE(ch.is_multi_destination());
}

TEST(UdpChannel, ParseResponseControlMode)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=224.0.1.1:40456|control-mode=response");
    EXPECT_TRUE(ch.is_response_control_mode());
    EXPECT_FALSE(ch.is_multi_destination());
}

TEST(UdpChannel, ParseWithTag)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|tag=42");
    EXPECT_TRUE(ch.has_tag());
    EXPECT_EQ(ch.tag(), 42);
}

TEST(UdpChannel, ParseWithGroupTag)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|group-tag=100");
    EXPECT_TRUE(ch.group_tag().has_value());
    EXPECT_EQ(ch.group_tag().value(), 100);
}

TEST(UdpChannel, ParseWithBufferSizes)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:40456|so-sndbuf=2m|so-rcvbuf=1m|rcv-wnd=512k");
    EXPECT_EQ(ch.socket_sndbuf_length(), 2 * 1024 * 1024);
    EXPECT_EQ(ch.socket_rcvbuf_length(), 1 * 1024 * 1024);
    EXPECT_EQ(ch.receiver_window_length(), 512 * 1024);
}

TEST(UdpChannel, ParseWithTimestampOffsets)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:40456|channel-rcv-ts-offset=0|channel-snd-ts-offset=0");
    EXPECT_TRUE(ch.is_channel_receive_timestamp_enabled());
    EXPECT_TRUE(ch.is_channel_send_timestamp_enabled());
}

TEST(UdpChannel, TimestampOffsetsDefaultDisabled)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    EXPECT_FALSE(ch.is_channel_receive_timestamp_enabled());
    EXPECT_FALSE(ch.is_channel_send_timestamp_enabled());
}

TEST(UdpChannel, CanonicalFormEquality)
{
    auto ch1 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    auto ch2 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    // Same URI parameters must produce the same canonical form (deterministic)
    EXPECT_EQ(ch1.canonical_form(), ch2.canonical_form());
    EXPECT_EQ(ch1, ch2);
}

TEST(UdpChannel, OriginalUriPreserved)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    EXPECT_EQ(ch.original_uri_string(), "aeron:udp?endpoint=127.0.0.1:40456");
}

TEST(UdpChannel, HasGroupSemantics)
{
    auto ch1 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|group-tag=100");
    EXPECT_TRUE(ch1.has_group_semantics());

    auto ch2 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|tag=1");
    EXPECT_TRUE(ch2.has_group_semantics());

    auto ch3 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    EXPECT_FALSE(ch3.has_group_semantics());
}

TEST(UdpChannel, SocketBufferDefaults)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    EXPECT_EQ(ch.socket_rcvbuf_length(), 0);
    EXPECT_EQ(ch.socket_sndbuf_length(), 0);
    EXPECT_EQ(ch.socket_rcvbuf_length_or_default(65536), 65536);
    EXPECT_EQ(ch.socket_sndbuf_length_or_default(65536), 65536);
}

TEST(UdpChannel, Description)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");
    auto desc = ch.description();
    EXPECT_NE(desc.find("UdpChannel"), std::string::npos);
}

TEST(UdpChannel, MulticastControlAddress)
{
    struct sockaddr_storage addr{};
    auto* a4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    a4->sin_family = AF_INET;
    a4->sin_addr.s_addr = htonl(0xE0000001);  // 224.0.0.1

    auto control = UdpChannel::get_multicast_control_address(addr);
    auto* c4 = reinterpret_cast<const struct sockaddr_in*>(&control);
    // Last octet should be 2 (224.0.0.1 -> 224.0.0.2)
    auto last_octet = reinterpret_cast<const u8*>(&c4->sin_addr.s_addr)[3];
    EXPECT_EQ(last_octet, 2);
}

// HIGH-1: Verify that parse_size_value does not cause signed integer overflow UB.
// A large base value multiplied by a large multiplier (e.g. 'm' = 1024*1024)
// must saturate to INT_MAX rather than overflow.
TEST(UdpChannel, ParseSizeValueOverflowSaturates)
{
    // "9223372036854775m" would overflow i64 during multiplication without the fix.
    // With the fix, it should saturate to INT_MAX.
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:0|so-sndbuf=9223372036854775m");
    EXPECT_EQ(ch.socket_sndbuf_length(), std::numeric_limits<int>::max());
}

// HIGH-1: Verify normal 'm' suffix values still work correctly.
TEST(UdpChannel, ParseSizeValueMegaSuffix)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:0|so-sndbuf=2m");
    EXPECT_EQ(ch.socket_sndbuf_length(), 2 * 1024 * 1024);
}

// HIGH-1: Verify normal 'k' suffix values still work correctly.
TEST(UdpChannel, ParseSizeValueKiloSuffix)
{
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:0|so-rcvbuf=512k");
    EXPECT_EQ(ch.socket_rcvbuf_length(), 512 * 1024);
}

// HIGH-1: Verify that a value that overflows with 'k' suffix also saturates.
TEST(UdpChannel, ParseSizeValueKiloOverflowSaturates)
{
    // 9223372036854 * 1024 would overflow i64
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:0|so-sndbuf=9223372036854k");
    EXPECT_EQ(ch.socket_sndbuf_length(), std::numeric_limits<int>::max());
}

// MEDIUM-1: Verify that invalid numeric parameters throw with identifiable messages
TEST(UdpChannel, ParseInvalidMulticastTtlThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|multicast-ttl=abc"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseInvalidGroupTagThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|group-tag=notanumber"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseInvalidNakDelayThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|nak-delay=xyz"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseInvalidTagThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|tag=abc"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseInvalidRcvTsOffsetThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|channel-rcv-ts-offset=abc"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseInvalidSndTsOffsetThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|channel-snd-ts-offset=abc"),
        std::invalid_argument);
}

// Finding 1: Invalid URI prefix must throw
TEST(UdpChannel, ParseInvalidPrefixThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("http:udp?endpoint=127.0.0.1:40456"),
        std::invalid_argument);
}

TEST(UdpChannel, ParseMissingPrefixThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("endpoint=127.0.0.1:40456"),
        std::invalid_argument);
}

// Finding 1: Pipe segment without '=' must throw
TEST(UdpChannel, ParsePipeSegmentMissingEqualsThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|badsegment"),
        std::invalid_argument);
}

// Finding 1: Missing endpoint for unicast (no tag, no MDC) must throw
TEST(UdpChannel, ParseMissingEndpointForUnicastThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp"),
        std::invalid_argument);
}

// Finding 1: Dynamic control mode without control must throw
TEST(UdpChannel, ParseDynamicModeWithoutControlThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=224.0.1.1:40456|control-mode=dynamic"),
        std::invalid_argument);
}

// Finding 1: MDC without endpoint must throw
TEST(UdpChannel, ParseMdcWithoutEndpointThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?control-mode=manual|control=224.0.1.1:40457"),
        std::invalid_argument);
}

// Finding 1: Tag-only channel is valid (no endpoint required)
TEST(UdpChannel, ParseTagOnlyIsValid)
{
    auto ch = UdpChannel::parse("aeron:udp?tag=42");
    EXPECT_TRUE(ch.has_tag());
    EXPECT_EQ(ch.tag(), 42);
}

// Finding 1: Manual control mode without endpoint or control is valid
TEST(UdpChannel, ParseManualModeOnlyIsValid)
{
    auto ch = UdpChannel::parse("aeron:udp?control-mode=manual");
    EXPECT_TRUE(ch.is_manual_control_mode());
}

// Finding 7: Multicast address with last octet 255 must throw
TEST(UdpChannel, MulticastControlAddressLastOctet255Throws)
{
    struct sockaddr_storage addr{};
    auto* a4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    a4->sin_family = AF_INET;
    a4->sin_addr.s_addr = htonl(0xE00000FF);  // 224.0.0.255

    EXPECT_THROW((void)UdpChannel::get_multicast_control_address(addr), std::invalid_argument);
}

// Finding 9: Negative multicast-ttl must throw
TEST(UdpChannel, ParseNegativeMulticastTtlThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=224.0.1.1:40456|multicast-ttl=-1"),
        std::invalid_argument);
}

// Finding 9: Negative so-sndbuf must throw
TEST(UdpChannel, ParseNegativeSndbufThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|so-sndbuf=-1"),
        std::invalid_argument);
}

// Finding 9: Negative rcv-wnd must throw
TEST(UdpChannel, ParseNegativeRcvWndThrows)
{
    EXPECT_THROW(
        UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456|rcv-wnd=-1"),
        std::invalid_argument);
}
